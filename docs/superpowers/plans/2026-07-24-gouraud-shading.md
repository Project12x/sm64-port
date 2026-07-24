# Gouraud Shading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-vertex lit Gouraud shading in the sourceboot Fast3D-to-VDP1 pipeline — which is also the fix for the frontend currently misreading vertex normals as RGB colors.

**Architecture:** Mirror `src/pc/gfx/gfx_pc.c`'s lighting semantics in Q16.16 (spec: `docs/superpowers/specs/2026-07-24-gouraud-shading-design.md`). Light state decodes from the two currently-ignored commands (`G_MOVEWORD` numLights, `G_MOVEMEM G_MV_LIGHT`); light directions re-transform lazily per modelview change; per-vertex colors compute at `G_VTX` time into the existing vertex struct; resolved triangles carry 3 corner colors; the emit path writes one 8-byte Gouraud table per triangle into a frame-local staging bank uploaded once per frame (used-prefix) through the SlaveDriver DMA queue, and emits `CC_GOURAUD` commands. Degrade-first contract: fog dropped, one directional light, bank overflow → flat fallback — everything counted.

**Tech Stack:** C11 host tests (`verify-runtime-contracts`, compiles `saturn_fast3d_frontend.c` + `saturn_trig_q16.inc.c` — NOT the Yaul-dependent emit TU), Q16 kernels from the matrix sprint, Yaul VDP1 (`vdp1_vram_partitions_get`, `vdp1_cmdt_gouraud_base_set`, `VDP1_CMDT_CC_GOURAUD`), GPL SlaveDriver DMA queue (`src/port/saturn/gpl/slavedriver_dma_queue.h`), pinned SH-2 GCC 14.3.0.

**Ground truth this plan rests on** (verified against the working tree 2026-07-24, post-`v0.1.0`):
- Reference math: `gfx_pc.c:534-542` (`calculate_normal_dir` — light dir /127, transformed by the TRANSPOSE of the modelview top, normalized), `:626-657` (ambient init from `current_lights[num-1].col`, per-light `intensity = dot(n, coeff)/127`, accumulate only if `> 0`, clamp 255), `:589-590` (every modelview `G_MTX` sets `lights_changed`), `:957-987` (`G_MV_LIGHT`: `lightidx = offset/24 - 2`, memcpy `sizeof(Light_t)`=12), `:989-1000` (`G_MW_NUMLIGHT` under F3DEX_GBI_2: `num = data/24 + 1`, includes ambient).
- Wire format: `Light_t` = col[3],pad,colc[3],pad,dir[3](signed),pad — 12 bytes (`gbi.h:1398-1407`); `gSPLight` sends `G_MOVEMEM` with idx=`G_MV_LIGHT`(10) in w0 bits 0-7, `ofs/8` in bits 8-15 (`gbi.h:2555-2558`, `:1801-1807`); `NUML(n) = n*24` (`gbi.h:2515-2519`).
- Frontend today: `G_MOVEWORD` has NO case (silently ignored); `G_MOVEMEM` handles only viewport (`saturn_fast3d_frontend.c:595-627`); `G_VTX` reads `.cn` unconditionally (`:628-673`); resolve packs one flat `color_rgb1555` from vertex 0 (`:425-445`); the geometry-mode word is stored (`:685-686`) but `G_LIGHTING` never consulted.
- Emit today: flat `CC_REPLACE` (`saturn_fast3d_vdp1_emit.c`, 59 lines, full file read).
- sourceboot has NO VDP1 partition setup (`vdp1_vram_partitions_get` never called) and does NOT yet link the DMA queue; commands CPU-copy to `VDP1_VRAM(0)`. hwtest proves the partition+DMA gouraud upload pattern (`hwtest/main.c:297-306`).
- Neutral base `0xC210` exists (`saturn_gouraud.h`); `vdp1_cmdt_gouraud_base_set` stores `(base>>3)&0xFFFF` (`libyaul .../cmdt.h:410-416`); `vdp1_gouraud_table_t` = 4×rgb1555, 8 bytes, 8-aligned (`.../vram.h:32-34`).

**File structure** (created/modified this plan):
- Create `src/port/saturn/gfx/saturn_light_q16.h` — light state + Q16 evaluator (Yaul-free, host-testable).
- Create `src/port/saturn/gfx/saturn_gouraud_bank.h` — staging-bank bookkeeping (Yaul-free, host-testable).
- Modify `src/port/saturn/gfx/saturn_fast3d_frontend.h/.c` — decode extensions, lit/unlit gating, corner colors, counters.
- Modify `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h/.c` — Gouraud emission + bank upload.
- Modify `src/port/saturn/sourceboot/main.c` + `Makefile` — partitions, DMA queue init/link, bank storage.
- Modify `tools/saturn/runtime_contract_test.c` — all host tests.

**Standing workflow facts** (from the matrix sprint — reuse, don't rediscover): host suite command is `export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"` then `make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe` from the repo root (Git-Bash). Cross-compile: `C:/msys64/usr/bin/bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/src/port/saturn/sourceboot && source ../../../../.yaul.env && make -j2 && make verify"` (`.yaul.env` vars don't propagate across the Git-Bash→msys64 boundary; run the whole sequence in one msys64 shell). No pytest exists anywhere in this repo.

---

### Task 1: Q16 light evaluator with float differential reference

**Files:**
- Create: `src/port/saturn/gfx/saturn_light_q16.h`
- Test: `tools/saturn/runtime_contract_test.c` (append)

- [x] **Step 1: Read the reference before writing anything**

Read `src/pc/gfx/gfx_pc.c`: `calculate_normal_dir` (~:534-542), `gfx_transposed_matrix_mul` (just above it — get the EXACT transpose orientation), `gfx_normalize_vector`, and the lighting block in `gfx_sp_vertex` (~:626-657). The float reference test below must be derived from that real code, and the Q16 evaluator written to match the reference — never the reverse; a mutual-inversion bug (both sides transposed the same wrong way) would pass the diff test while diverging from gfx_pc. Also read `levels/bob/areas/1/1/model.inc.c:2` and copy the real `gdSPDefLights1` argument values for use as a test fixture.

- [x] **Step 2: Write the failing tests**

Append to `tools/saturn/runtime_contract_test.c` (after the last `test_kernels_*` function, before `main()`), and add `#include "saturn_light_q16.h"` next to the existing gfx includes at the top:

```c
/* Float mirror of gfx_pc.c's lighting math (calculate_normal_dir at
 * :534-542 + the gfx_sp_vertex lighting block at :626-657), used as the
 * differential ground truth for the Q16 evaluator. Verify the transpose
 * orientation against the real gfx_transposed_matrix_mul before
 * trusting this: coeffs[i] = sum_j M[j][i] * light_dir[j]. */
static void ref_light_eval(const int8_t light_dir[3],
                           const uint8_t light_col[3],
                           const uint8_t amb_col[3],
                           const sm64_saturn_mtx_t *mv,
                           const int8_t n[3], uint8_t out_rgb[3])
{
    float ld[3] = { light_dir[0] / 127.0f, light_dir[1] / 127.0f,
                    light_dir[2] / 127.0f };
    float c[3];
    float mag;
    float intensity;
    int ch[3];

    for (int i = 0; i < 3; i++) {
        c[i] = ld[0] * sm64_saturn_q16_to_float(mv->m[0][i])
             + ld[1] * sm64_saturn_q16_to_float(mv->m[1][i])
             + ld[2] * sm64_saturn_q16_to_float(mv->m[2][i]);
    }
    mag = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    if (mag != 0.0f) {
        c[0] /= mag; c[1] /= mag; c[2] /= mag;
    }
    intensity = (n[0] * c[0] + n[1] * c[1] + n[2] * c[2]) / 127.0f;
    for (int i = 0; i < 3; i++) {
        ch[i] = amb_col[i];
        if (intensity > 0.0f) {
            ch[i] += (int)(intensity * light_col[i]);
        }
        out_rgb[i] = ch[i] > 255 ? 255 : (uint8_t)ch[i];
    }
}

static void test_light_q16_matches_float_reference(void)
{
    /* Real BOB Lights1 values -- copy the actual numbers from
     * levels/bob/areas/1/1/model.inc.c:2's gdSPDefLights1 when
     * implementing (Step 1 read them); the values below are the
     * placeholder SHAPE only and MUST be replaced with the real ones. */
    static const uint8_t amb[3] = { 0x33, 0x33, 0x33 };
    static const uint8_t col[3] = { 0xcc, 0xcc, 0xcc };
    static const int8_t dir[3] = { 0x28, 0x28, 0x28 };

    sm64_saturn_mtx_t mv[3];
    sm64_saturn_light_state_t st;
    int mi;

    sm64_saturn_matrix_identity(&mv[0]);
    sm64_saturn_mtxq_rotate_zxy_and_translate(&mv[1],
        (const int32_t[3]){ 0, 0, 0 }, 0x1234, -0x0800, 0x4000);
    /* non-uniform scale exercises the normalize step */
    sm64_saturn_mtxq_scale_vec3f(&mv[2], &mv[1],
        (const int32_t[3]){ 3 << 15, 1 << 16, 5 << 14 });

    for (mi = 0; mi < 3; mi++) {
        sm64_saturn_light_state_init(&st);
        memcpy(st.dir_col, col, 3);
        memcpy(st.amb_col, amb, 3);
        memcpy(st.dir_dir, dir, 3);
        st.num_lights = 2;
        st.lights_changed = true;
        sm64_saturn_light_recompute_coeffs(&st, &mv[mi]);
        assert(!st.lights_changed);

        for (int nx = -128; nx <= 127; nx += 24) {
            for (int ny = -128; ny <= 127; ny += 24) {
                for (int nz = -128; nz <= 127; nz += 24) {
                    const int8_t n[3] = { (int8_t)nx, (int8_t)ny,
                                          (int8_t)nz };
                    uint8_t want[3], got[3];
                    ref_light_eval(dir, col, amb, &mv[mi], n, want);
                    sm64_saturn_light_eval_vertex(&st, n, got);
                    for (int i = 0; i < 3; i++) {
                        int d = (int)got[i] - (int)want[i];
                        if (d < 0) { d = -d; }
                        assert(d <= 2); /* integer sqrt + Q16 rounding */
                    }
                }
            }
        }
    }
}

static void test_light_q16_zero_normal_gets_ambient(void)
{
    sm64_saturn_light_state_t st;
    sm64_saturn_mtx_t ident;
    static const int8_t zero_n[3] = { 0, 0, 0 };
    uint8_t got[3];

    sm64_saturn_matrix_identity(&ident);
    sm64_saturn_light_state_init(&st);
    st.amb_col[0] = 10; st.amb_col[1] = 20; st.amb_col[2] = 30;
    st.dir_col[0] = 200; st.dir_col[1] = 200; st.dir_col[2] = 200;
    st.dir_dir[2] = 127;
    st.num_lights = 2;
    st.lights_changed = true;
    sm64_saturn_light_recompute_coeffs(&st, &ident);
    sm64_saturn_light_eval_vertex(&st, zero_n, got);
    assert(got[0] == 10 && got[1] == 20 && got[2] == 30);
}
```

Register both in `main()` after `test_kernels_trig_lookup();`. Note the test uses `sqrtf`/`memcpy` — `<math.h>`/`<string.h>` are already included in this file; the recipe links `-lm`? Check: the `verify-runtime-contracts` recipe (`Makefile.saturn.mk:144-156`) does NOT pass `-lm` — add it to the recipe in the same style as `verify-mtxq-ctors` if the link fails on `sqrtf` (glibc-style toolchains often need it; MinGW typically doesn't — ground-truth by building).

- [x] **Step 3: Run to confirm failure**

Host suite command (see Standing workflow facts). Expected: FAIL to compile — `saturn_light_q16.h: No such file or directory`.

- [x] **Step 4: Implement the evaluator**

Create `src/port/saturn/gfx/saturn_light_q16.h`:

```c
#ifndef SM64_SATURN_LIGHT_Q16_H
#define SM64_SATURN_LIGHT_Q16_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_matrix.h"
#include "saturn_matrix_ctors.h" /* sm64_saturn_q16_vec3_normalize */

/* Q16.16 lighting state + per-vertex evaluator for the Fast3D frontend.
 *
 * Semantic anchor: src/pc/gfx/gfx_pc.c (the PC port's lowering of the
 * same display lists) -- calculate_normal_dir (:534-542) and the
 * gfx_sp_vertex lighting block (:626-657), mirrored formula-for-formula
 * in integer math, same discipline as saturn_matrix_ctors.h vs
 * math_util.c. Degradation contract (see the 2026-07-24 design spec):
 * exactly ONE directional light + ambient is evaluated; callers count
 * anything else rather than supporting it.
 *
 * Host-testable by design (no Yaul dependency). */

typedef struct sm64_saturn_light_state {
    uint8_t dir_col[3]; /* directional light color (Light_t.col) */
    int8_t dir_dir[3];  /* raw wire direction (Light_t.dir, s8) */
    uint8_t amb_col[3]; /* ambient color (Ambient_t.col) */
    uint8_t num_lights; /* gfx_pc convention: directional count + 1 */
    bool lights_changed;
    int32_t coeff_q16[3]; /* transformed+normalized dir, Q16.16 */
} sm64_saturn_light_state_t;

static inline void
sm64_saturn_light_state_init(sm64_saturn_light_state_t *st)
{
    for (int i = 0; i < 3; i++) {
        st->dir_col[i] = 0;
        st->dir_dir[i] = 0;
        st->amb_col[i] = 0;
        st->coeff_q16[i] = 0;
    }
    st->num_lights = 2;
    st->lights_changed = true;
}

/* Mirrors gfx_pc.c:534-542: the light DIRECTION (not every vertex
 * normal) is transformed by the TRANSPOSE of the modelview top --
 * coeff[i] = sum_j M[j][i] * dir[j] -- then normalized. The /127
 * folds gfx_pc's dir[]/127.0f pre-scale; normalization makes any
 * residual scale irrelevant. Products are Q16 * s8 -> at most
 * 2^31 * 127, summed x3: fits int64 trivially. The pre-scale loop
 * bounds the components before vec3_normalize, whose squared-sum
 * contract assumes unit-range inputs (saturn_matrix_ctors.h:28-31) --
 * scaled modelview entries can exceed that. */
static inline void
sm64_saturn_light_recompute_coeffs(sm64_saturn_light_state_t *st,
                                   const sm64_saturn_mtx_t *modelview_top)
{
    int32_t v[3];

    for (int i = 0; i < 3; i++) {
        int64_t sum = 0;
        for (int j = 0; j < 3; j++) {
            sum += (int64_t)modelview_top->m[j][i]
                 * (int64_t)st->dir_dir[j];
        }
        v[i] = (int32_t)(sum / 127);
    }
    for (;;) {
        int32_t a0 = v[0] < 0 ? -v[0] : v[0];
        int32_t a1 = v[1] < 0 ? -v[1] : v[1];
        int32_t a2 = v[2] < 0 ? -v[2] : v[2];
        if (a0 <= (1 << 20) && a1 <= (1 << 20) && a2 <= (1 << 20)) {
            break;
        }
        v[0] /= 2;
        v[1] /= 2;
        v[2] /= 2;
    }
    sm64_saturn_q16_vec3_normalize(v);
    st->coeff_q16[0] = v[0];
    st->coeff_q16[1] = v[1];
    st->coeff_q16[2] = v[2];
    st->lights_changed = false;
}

/* Mirrors gfx_pc.c:637-657: start from ambient, add
 * intensity * directional color when intensity = dot(n, coeff)/127
 * is positive, clamp each channel to 255. dot is s8 * Q16 summed x3
 * (fits int32 with margin, int64 used for uniformity); /127 matches
 * gfx_pc's normal scale. */
static inline void
sm64_saturn_light_eval_vertex(const sm64_saturn_light_state_t *st,
                              const int8_t n[3], uint8_t out_rgb[3])
{
    int64_t dot = (int64_t)n[0] * st->coeff_q16[0]
                + (int64_t)n[1] * st->coeff_q16[1]
                + (int64_t)n[2] * st->coeff_q16[2];
    int32_t intensity_q16 = (int32_t)(dot / 127);

    for (int i = 0; i < 3; i++) {
        int32_t ch = st->amb_col[i];
        if (intensity_q16 > 0) {
            ch += (int32_t)(((int64_t)intensity_q16
                             * st->dir_col[i]) >> 16);
        }
        out_rgb[i] = ch > 255 ? 255 : (uint8_t)ch;
    }
}

#endif
```

Before running: hand-trace the transpose orientation once against the real `gfx_transposed_matrix_mul` (Step 1) — if gfx_pc's helper is column-major or argument-swapped relative to the sketch above, fix BOTH the reference and the evaluator to match gfx_pc, and say so in the commit message.

- [x] **Step 5: Run tests to verify pass**

Host suite command. Expected: exit 0. If the |Δ|≤2 tolerance fails, investigate before loosening (likely a transpose-orientation mismatch or a missed /127) — the tolerance encodes a real precision claim.

- [x] **Step 6: Mutation check, then commit**

Apply each mutation to `saturn_light_q16.h`, re-run the host suite, confirm FAIL, revert: (a) remove the `intensity_q16 > 0` clamp; (b) swap `amb_col`/`dir_col` in the evaluator; (c) change `m[j][i]` to `m[i][j]` (transpose flip); (d) delete the `/ 127` in the dot. All four must fail. Restore byte-identical (verify with `git diff` — clean), then:

```bash
git add src/port/saturn/gfx/saturn_light_q16.h tools/saturn/runtime_contract_test.c Makefile.saturn.mk
git commit -m "feat(saturn): Q16 light evaluator mirroring gfx_pc.c, host-differential-tested"
```
(Drop `Makefile.saturn.mk` from the add if Step 2's `-lm` note proved unnecessary.)

Committed as `099b11d` (without `Makefile.saturn.mk` -- `-lm` proved
unnecessary on this toolchain). Reading the real `gfx_transposed_matrix_mul`
(Step 1) found the plan's sketch had the light-direction transpose
backwards (`M[j][i]` instead of the real `M[i][j]`) -- fixed in both the
float reference and the Q16 evaluator, independently re-verified by
spec-compliance review. Code-quality review then found a real
signed-overflow UB in the pre-normalize halving loop (`v[0] == INT32_MIN`
-- exactly what `sm64_saturn_float_to_q16`'s own saturation produces --
wraps the abs-value check and silently bypasses the loop). Fixed in
`a8dc0a4`, widening the whole computation to `int64_t` until safely
bounded (same idiom as `sm64_saturn_mtxq_lookat`), with two new
regression tests -- both verified by the implementer building them
against the *reverted* buggy code first, since the reviewer's own
originally-proposed test turned out not to discriminate. One cosmetic
comment fix (`2^39`->`2^40`) in `f4fffc5`.

---

### Task 2: Decode light commands (G_MOVEWORD numLights, G_MOVEMEM G_MV_LIGHT)

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h` (light state field + counters)
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c` (decode cases)
- Test: `tools/saturn/runtime_contract_test.c` (append)

- [x] **Step 1: Write the failing tests**

```c
static void test_frontend_decodes_lights(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* A real Lights1: ambient {10,20,30}, directional {200,210,220}
     * from direction {40,50,60}. Layout per gbi.h:1398-1441. */
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 40, 50, 60);
    Gfx list[4];
    struct SPTask task;

    /* gSPNumLights(1): G_MOVEWORD, index=G_MW_NUMLIGHT in C0(16,8),
     * offset=G_MWO_NUMLIGHT in C0(0,16), w1=NUML(1)=24. Verify the
     * dispatch field positions against gfx_pc.c's G_MOVEWORD case
     * before trusting this encoding. */
    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24; /* NUML(1) */
    /* gSPLight(&lights.l[0], 1): idx=G_MV_LIGHT, ofs=(1)*24+24=48,
     * w0 bits 8-15 carry ofs/8=6 (gbi.h:2555-2558 + gDma2p :1801-1807) */
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    /* gSPLight(&lights.a, 2): ofs=(2)*24+24=72, ofs/8=9 */
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.lights.num_lights == 2);
    assert(frontend.lights.dir_col[0] == 200 &&
           frontend.lights.dir_col[1] == 210 &&
           frontend.lights.dir_col[2] == 220);
    assert(frontend.lights.dir_dir[0] == 40 &&
           frontend.lights.dir_dir[1] == 50 &&
           frontend.lights.dir_dir[2] == 60);
    assert(frontend.lights.amb_col[0] == 10 &&
           frontend.lights.amb_col[1] == 20 &&
           frontend.lights.amb_col[2] == 30);
    assert(frontend.lights.lights_changed);
    assert(frontend.profile.unsupported_num_lights == 0);
}

static void test_frontend_counts_unsupported_num_lights(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[2];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 48; /* NUML(2): two directionals -> unsupported */
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.unsupported_num_lights == 1);
    assert(frontend.lights.num_lights == 2); /* clamped to 1 directional */
}
```

Register both in `main()`. `gdSPDefLights1` availability: it is a gbi.h macro producing a `Lights1` initializer — if it does not expand cleanly in the test TU, hand-write the `Lights1` initializer instead (col/colc duplicated, layout per `gbi.h:1398-1441`) and say so in the commit message.

- [x] **Step 2: Run to confirm failure**

Expected: FAIL to compile — `frontend.lights` / `unsupported_num_lights` don't exist yet.

- [x] **Step 3: Implement**

In `saturn_fast3d_frontend.h`:
- Add `#include "saturn_light_q16.h"` next to the existing includes.
- In `sm64_saturn_fast3d_frontend_t` (`:298-307`), add `sm64_saturn_light_state_t lights;` after `geometry_mode` (persists across the per-frame profile memset, matching gfx_pc's persistent light state — the reset at `saturn_fast3d_frontend.c:745-755` doesn't touch it).
- In `sm64_saturn_fast3d_profile_t`, **append at the very end of the struct** (existing capture-decode scripts hand-map earlier offsets; appending keeps them valid — standing lesson): `uint32_t unsupported_num_lights; uint32_t lit_vertices; uint32_t unlit_vertices; uint32_t fog_dropped_triangles; uint32_t gouraud_bank_overflow;` with a comment citing the design spec's degradation contract. (All five now; Tasks 3/4/6 consume the rest.)
- In `sm64_saturn_fast3d_frontend_init` (find it in the .c), add `sm64_saturn_light_state_init(&frontend->lights);`.

In `saturn_fast3d_frontend.c`:
- New case in `sm64_saturn_fast3d_decode_command` (next to `G_MOVEMEM`):

```c
        case G_MOVEWORD: {
            /* gfx_pc.c dispatch: index = C0(16,8), data = w1.
             * G_MW_NUMLIGHT under F3DEX_GBI_2: num = w1/24 + 1
             * (gfx_pc.c:989-1000; NUML(n)=n*24, gbi.h:2515-2519).
             * Degradation contract: exactly 1 directional supported;
             * anything else is counted and clamped, first light used. */
            const uint8_t mw_index = (uint8_t)SM64_SATURN_C0(w0, 16, 8);
            if (mw_index == G_MW_NUMLIGHT) {
                const uint8_t num = (uint8_t)(w1 / 24U + 1U);
                if (num != 2U) {
                    profile->unsupported_num_lights++;
                }
                frontend->lights.num_lights = 2U;
                frontend->lights.lights_changed = true;
            }
            break;
        }
```

- Extend the `G_MOVEMEM` case (after the viewport `if`):

```c
            else if (index == G_MV_LIGHT) {
                /* gSPLight wire: w0 bits 8-15 = byte-offset/8
                 * (gDma2p, gbi.h:1801-1807); lightidx = offset/24 - 2
                 * (gfx_pc.c:972). idx 0 = the one supported
                 * directional; the ambient always arrives at slot
                 * num_lights-1 = 1. Copy the 12-byte Light_t payload
                 * via memcpy (strict-aliasing-safe); for the ambient
                 * this reads 4 bytes past Ambient_t's 8 -- same as
                 * gfx_pc.c:975's documented behavior, safe here
                 * because SM64 ambients live inside a Lights1 with
                 * the directional Light contiguous after them
                 * (gbi.h:1438-1441). Only col[0..2] is read from the
                 * ambient slot, so the extra bytes are never used. */
                const uint32_t offset = SM64_SATURN_C0(w0, 8, 8) * 8U;
                const int32_t lightidx = (int32_t)(offset / 24U) - 2;
                uint8_t raw[12];
                (void)memcpy(raw, (const void *)w1, sizeof(raw));
                if (lightidx == 0) {
                    frontend->lights.dir_col[0] = raw[0];
                    frontend->lights.dir_col[1] = raw[1];
                    frontend->lights.dir_col[2] = raw[2];
                    frontend->lights.dir_dir[0] = (int8_t)raw[8];
                    frontend->lights.dir_dir[1] = (int8_t)raw[9];
                    frontend->lights.dir_dir[2] = (int8_t)raw[10];
                } else {
                    frontend->lights.amb_col[0] = raw[0];
                    frontend->lights.amb_col[1] = raw[1];
                    frontend->lights.amb_col[2] = raw[2];
                }
                frontend->lights.lights_changed = true;
            }
```

- In the `G_MTX` modelview branch (the `} else {` at `:527`) add `frontend->lights.lights_changed = true;` at its end (mirrors `gfx_pc.c:590`), and the same line in the `G_POPMTX` case (`:588`) — gfx_pc omits pop, but a pop changes the top matrix and stale coeffs would be silently wrong; deliberate, documented improvement, note it in the commit.
- `G_MOVEWORD` needs `memcpy` → confirm `<string.h>` is included in the .c (it is, since Task 5 of the wire-format sprint touched it — verify).
- Check `count_command`'s comment at `:73` listing G_MOVEWORD as "other" — update the comment (it now has a real decode case).

- [x] **Step 4: Run tests to verify pass**

Host suite: exit 0, all tests including Task 1's.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.h src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): decode G_MOVEWORD numLights and G_MV_LIGHT into frontend light state"
```

Committed as `30d8d25`. Reading the real reference caught something the
sketch's bare `else` for the ambient branch missed:
`rendering_graph_node.c` unconditionally emits `gSPLookAt` every frame
under this build's dialect, which sends two more `G_MOVEMEM`/`G_MV_LIGHT`
commands at `lightidx` -2/-1 (matching `gfx_pc.c`'s own `lightidx >= 0`
"skip lookat" guard) -- the sketch would have let those silently
overwrite `amb_col` with direction bytes every frame. Fixed with an
explicit `lightidx == 0 || lightidx == 1` guard, independently
re-verified by spec-compliance review via an actual revert-and-rebuild.
Code-quality review then found two of the three new tests asserted
fields already true from `sm64_saturn_light_state_init`'s own defaults,
making them unable to detect their own decode logic being deleted --
fixed in `387bd04` by seeding non-default values before `submit()`,
confirmed both ways (revert the decode -> test fails; keep the fix ->
test passes).

---

### Task 3: Lit/unlit G_VTX gating — the normals-as-colors bug fix

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c` (G_VTX case)
- Test: `tools/saturn/runtime_contract_test.c` (append)

- [x] **Step 1: Write the failing tests**

```c
static void test_frontend_lit_vertex_evaluates_lighting(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 0, 0, 127);
    /* Vtx_tn shape: same bytes as Vtx_t.cn, interpreted as s8 normal
     * (0,0,127) = straight +Z, alpha 255. */
    static const Vtx_t vert = {
        .ob = { 1.0f, 2.0f, 3.0f }, .flag = 0, .tc = { 0, 0 },
        .cn = { 0, 0, 127, 255 }
    };
    Gfx list[6];
    struct SPTask task;
    uint8_t want[3];
    sm64_saturn_mtx_t ident;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24;
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    /* set G_LIGHTING: keep-mask C0(0,24)=0xFFFFFF (clear nothing),
     * set-bits w1 = G_LIGHTING */
    list[3].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[3].words.w1 = G_LIGHTING;
    list[4].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[4].words.w1 = (uintptr_t)&vert;
    list[5] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    sm64_saturn_matrix_identity(&ident);
    ref_light_eval((const int8_t[3]){ 0, 0, 127 },
                   (const uint8_t[3]){ 200, 210, 220 },
                   (const uint8_t[3]){ 10, 20, 30 },
                   &ident, (const int8_t[3]){ 0, 0, 127 }, want);
    for (int i = 0; i < 3; i++) {
        int d = (int)frontend.vertices[0].r - (int)want[0];
        (void)d; /* per-channel below */
    }
    assert((int)frontend.vertices[0].r - (int)want[0] <= 2 &&
           (int)want[0] - (int)frontend.vertices[0].r <= 2);
    assert((int)frontend.vertices[0].g - (int)want[1] <= 2 &&
           (int)want[1] - (int)frontend.vertices[0].g <= 2);
    assert((int)frontend.vertices[0].b - (int)want[2] <= 2 &&
           (int)want[2] - (int)frontend.vertices[0].b <= 2);
    assert(frontend.vertices[0].a == 255);
    assert(frontend.profile.lit_vertices == 1);
    assert(frontend.profile.unlit_vertices == 0);
    assert(!frontend.lights.lights_changed); /* lazy recompute ran */
}

static void test_frontend_unlit_vertex_passes_colors_through(void)
{
    /* No G_LIGHTING: existing behavior, cn bytes are true RGBA. */
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t vert = {
        .ob = { 0.0f, 0.0f, 0.0f }, .flag = 0, .tc = { 0, 0 },
        .cn = { 12, 34, 56, 78 }
    };
    Gfx list[2];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[0].words.w1 = (uintptr_t)&vert;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.vertices[0].r == 12 && frontend.vertices[0].g == 34 &&
           frontend.vertices[0].b == 56 && frontend.vertices[0].a == 78);
    assert(frontend.profile.unlit_vertices == 1);
    assert(frontend.profile.lit_vertices == 0);
}
```

Register both in `main()`.

- [x] **Step 2: Run to confirm failure**

Expected: `test_frontend_lit_vertex_evaluates_lighting` FAILS at the color assertions (current code stores the raw normal bytes {0,0,127} as color; the lit expectation differs) and at the `lit_vertices` counter. The unlit test may already pass except its counter.

- [x] **Step 3: Implement**

In the `G_VTX` case (`saturn_fast3d_frontend.c:628-673`), before the vertex loop:

```c
            const bool lit =
                (frontend->geometry_mode & G_LIGHTING) != 0U;
            if (lit && frontend->lights.lights_changed) {
                /* Lazy, once per change (gfx_pc.c:627-635): light
                 * DIRECTIONS re-transform against the current
                 * modelview top; vertex normals stay raw. */
                sm64_saturn_light_recompute_coeffs(
                    &frontend->lights,
                    sm64_saturn_matrix_stack_top(&frontend->matrix_stack));
            }
```

Inside the loop, replace the four `cn` copy lines with:

```c
                if (lit) {
                    /* Under G_LIGHTING the trailing Vtx bytes are a
                     * packed s8 normal (Vtx_tn, gbi.h:1134), NOT a
                     * color -- reading them as RGB was the pre-Gouraud
                     * bug this branch fixes. The uint8->int8 casts
                     * rely on GCC's two's-complement conversion
                     * (project is GCC-only, both host and SH-2). */
                    const int8_t n[3] = { (int8_t)src[i].cn[0],
                                          (int8_t)src[i].cn[1],
                                          (int8_t)src[i].cn[2] };
                    uint8_t rgb[3];
                    sm64_saturn_light_eval_vertex(&frontend->lights, n,
                                                  rgb);
                    frontend->vertices[dest].r = rgb[0];
                    frontend->vertices[dest].g = rgb[1];
                    frontend->vertices[dest].b = rgb[2];
                    frontend->vertices[dest].a = src[i].cn[3];
                    profile->lit_vertices++;
                } else {
                    frontend->vertices[dest].r = src[i].cn[0];
                    frontend->vertices[dest].g = src[i].cn[1];
                    frontend->vertices[dest].b = src[i].cn[2];
                    frontend->vertices[dest].a = src[i].cn[3];
                    profile->unlit_vertices++;
                }
```

Also update the `Vtx_t` cast comment at `:638` — it currently asserts "Vtx_t layout, not Vtx_tn", which is now only half-true (the byte layout is shared; interpretation is G_LIGHTING-gated).

- [x] **Step 4: Run tests, then mutation check**

Host suite: exit 0, everything green. Mutations (revert each after confirming a test fails): (a) invert the `lit` condition; (b) delete the lazy `recompute` call; (c) store `n[0..2]` raw instead of `rgb[0..2]`. All must fail.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "fix(saturn): gate G_VTX on G_LIGHTING -- evaluate normals, stop misreading them as RGB"
```

Committed as `d69927a`. Unlike Tasks 1-2, every symbol this sketch
depended on matched the real code exactly on inspection: `G_LIGHTING`
(gbi.h:364), `sm64_saturn_matrix_stack_top`'s real signature
(saturn_matrix.h:250, already used identically in this file's own
G_MTX case), `sm64_saturn_light_recompute_coeffs`/`_eval_vertex`'s
signatures (saturn_light_q16.h -- already carrying Task 1's transpose
fix, so no repeat of that bug here), and the Vtx_t/Vtx_tn shared-byte-
layout claim (gbi.h:1112-1136) all held as written -- no functional
deviation this task. Two small, deliberate departures from the sketch's
literal text: (1) the lazy-recompute block was placed after the
`n_vertices > end_index` underflow-reject guard rather than at the very
top of the case, so a malformed/rejected G_VTX command doesn't spend a
recompute on vertices that will never be processed -- behaviorally
identical to the sketch for every well-formed command; (2) the first
test's sketch included a vestigial no-op loop (`for (int i = 0; i < 3;
i++) { int d = ...r - ...want[0]; (void)d; }`) that never used its own
loop index and discarded its result unconditionally -- dead code
asserting nothing, dropped in favor of the three explicit per-channel
tolerance asserts already immediately following it (same class of
test-quality issue Task 2's review flagged, fixed proactively here
instead of waiting for review). TDD followed for real: reverted just
the `.c` change (keeping the new tests via `git checkout --` +
`git apply` of a saved patch), confirmed
`test_frontend_lit_vertex_evaluates_lighting` failed at its first color
assertion for the expected reason (raw normal byte 0 read back as red
instead of the ~210 lit value), then reapplied. All three required
mutations confirmed to fail: (a) (invert `lit`) was actually caught
first by the pre-existing `test_frontend_g_vtx_transform` rather than
this task's own new tests, since `assert()` aborts at the first failure
in `main()`'s call order and that earlier unlit-fixture test
(`geometry_mode == 0`) is equally misclassified as lit by the same
inversion -- by inspection the new unlit test here has the identical
vulnerability and would fail the same way had it run first; (b)
(delete the recompute) and (c) (store the raw normal instead of the
evaluated color) both failed directly at this task's new lit-vertex
test's color assertion, as expected. Every revert verified
byte-identical via a saved-patch diff, and the full suite (Tasks 1-2
included) passed clean under `-Wall -Wextra -Werror` before and after.

---

### Task 4: Corner colors in the resolved triangle + fog counter

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h` (struct)
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c` (resolve)
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c` (keep compiling: corner[0] as interim flat color)
- Test: `tools/saturn/runtime_contract_test.c`

- [x] **Step 1: Find every consumer of the old field**

```
grep -rn "color_rgb1555" src/ tools/
```
Expected consumers: the resolve write (`saturn_fast3d_frontend.c:433`), the emit read (`saturn_fast3d_vdp1_emit.c:~50`), possibly test assertions. Every hit must be updated this task — list them in the commit message.

- [x] **Step 2: Write the failing test**

```c
static void test_frontend_resolved_triangle_carries_corner_colors(void)
{
    /* Reuses test_frontend_g_tri1_resolves_triangle's fixture shape:
     * three unlit verts colored pure red/green/blue must arrive as
     * three DISTINCT packed corner colors, in vertex order. */
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[3] = {
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = { 100.0f, -100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {   0.0f,  100.0f, 500.0f}, .cn = {0, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[4];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);
    list[1].words.w1 = (uintptr_t)verts;
    list[2].words.w0 = ((uint32_t)G_TRI1 << 24) |
                       (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = 0;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.resolved_count == 1);
    assert(frontend.resolved[0].corner_rgb1555[0] == 0xFC00); /* red   */
    assert(frontend.resolved[0].corner_rgb1555[1] == 0x83E0); /* green */
    assert(frontend.resolved[0].corner_rgb1555[2] == 0x801F); /* blue  */
}

static void test_frontend_counts_dropped_fog(void)
{
    /* Same fixture, plus G_FOG in the geometry mode: the triangle
     * still resolves (fog is dropped, not rejected) and the drop is
     * counted -- degradation contract. */
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[3] = {
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = { 100.0f, -100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {   0.0f,  100.0f, 500.0f}, .cn = {0, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[5];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[1].words.w1 = G_FOG;
    list[2].words.w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);
    list[2].words.w1 = (uintptr_t)verts;
    list[3].words.w0 = ((uint32_t)G_TRI1 << 24) |
                       (0U << 16) | (2U << 8) | (4U << 0);
    list[3].words.w1 = 0;
    list[4] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.resolved_count == 1);
    assert(frontend.profile.fog_dropped_triangles == 1);
}
```

Register both in `main()`.

- [x] **Step 3: Run to confirm failure**

Expected: FAIL to compile — `corner_rgb1555` doesn't exist.

- [x] **Step 4: Implement**

In `saturn_fast3d_frontend.h:233-238`, replace `uint16_t color_rgb1555;` with `uint16_t corner_rgb1555[3];` (struct grows 16→20 bytes; ×1536 resolved slots = +6 KiB static, fine against the ~191 KiB HWRAM margin measured after the `.lwram_bss` relocation — note this in the struct comment).

In the resolve function (`saturn_fast3d_frontend.c:425-445`), replace the single-color pack with:

```c
    /* Per-corner colors for VDP1 Gouraud (design spec 2026-07-24).
     * Bit 15 (0x8000) is the RGB flag -- see the original flat-color
     * comment's VDP2 sprite-layer rationale, which still applies to
     * every corner entry. */
    for (int c = 0; c < 3; c++) {
        const sm64_saturn_fast3d_vertex_t *v = &frontend->vertices[idx[c]];
        out->corner_rgb1555[c] = (uint16_t)(0x8000U |
            ((v->r >> 3) << 10) | ((v->g >> 3) << 5) | (v->b >> 3));
    }
    if ((frontend->geometry_mode & G_FOG) != 0U) {
        /* Degradation contract: fog is dropped, not rendered. */
        profile->fog_dropped_triangles++;
    }
```

Preserve the original comment block's VDP2 RGB-flag-bit war story (move it onto the loop). In `saturn_fast3d_vdp1_emit.c:~50`, change `tri->color_rgb1555` to `tri->corner_rgb1555[0]` with a `/* interim flat: real Gouraud lands with the bank task */` note. Update any test assertions found in Step 1.

- [x] **Step 5: Run host tests, then cross-compile**

Host suite: exit 0. Then the full SH-2 cross-compile + `make verify` (Standing workflow facts) — the emit TU only compiles there. Both exit 0.

- [x] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.h src/port/saturn/gfx/saturn_fast3d_frontend.c src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): resolved triangles carry per-corner colors; count dropped fog"
```

Committed as `a6e8121`. Like Task 3, no functional deviation from the
sketch: the real `idx[3]`/`geometry_mode`/`G_FOG` usage all matched
what was sketched on inspection, and the grep for `color_rgb1555`
turned up exactly the 5 hits the plan predicted (resolve write, field
decl, emit read, one test assertion — all updated).

Two-stage review: spec-compliance review independently re-ran the
`color_rgb1555` grep, re-read the per-frame `memset` reset to confirm
`fog_dropped_triangles` is genuinely non-vacuous, independently
reproduced both claimed mutations (each correctly fails its target
test), and independently reproduced both the host suite and the full
SH-2 cross-compile + `make verify` (rather than trusting the reported
exit codes) — all confirmed. It also caught two things worth recording
here: (1) a separate, older HWRAM-budget comment elsewhere in
`saturn_fast3d_frontend.h` (line ~313) still cited the struct's old
16-bytes/entry size after this task grew it to 20 — fixed directly as
a small follow-up commit `27918e5`, not folded into `a6e8121` per this
project's no-amend policy; (2) the commit message's build-verification
note is wrong about which artifact is 640K — that figure belongs to
the separate program `.bin` (869,956 bytes, from Yaul's own
`build.post.bin.mk`), not `SOURCE.DAT`. The real, independently
re-measured `SOURCE.DAT` is **1,715,488 bytes (~1.7 MiB)** — still
comfortably under the 4 MiB cap, so `make verify`'s pass/fail verdict
was never wrong, only the number cited for it. Left uncorrected in the
commit message itself (no amending already-created commits), recorded
accurately here instead.

Code-quality review: Ready to merge — Yes, no Critical or Important
issues. Independently re-verified (not just re-read) the "5 consumers"
grep against the base commit, the corner-index correspondence between
`corner_rgb1555[c]` and the pre-existing `x[c]`/`y[c]` pipeline, the
RGB1555 bit-packing by hand for all three test colors, and the
fog-counter non-vacuousness via the same `memset` trace. Three
zero-impact Minor notes only (a stylistic two-loop-vs-one-loop
observation, a "Task 6 vs. Task 5" wording nit in a comment copied
verbatim from the plan's own text, and a process observation that a
future plan should tell implementers to grep a whole file for stale
byte-count comments whenever a struct's `sizeof` changes) — none
required a fix round.

---

### Task 5: Gouraud staging bank (bookkeeping, host-tested)

**Files:**
- Create: `src/port/saturn/gfx/saturn_gouraud_bank.h`
- Test: `tools/saturn/runtime_contract_test.c` (append)

- [x] **Step 1: Write the failing tests**

```c
static void test_gouraud_bank_alloc_and_used_prefix(void)
{
    static sm64_saturn_gouraud_table_t staging[4];
    sm64_saturn_gouraud_bank_t bank;
    uintptr_t addr0, addr1;
    sm64_saturn_gouraud_table_t *t;

    assert(sm64_saturn_gouraud_bank_init(&bank, staging, 4, 0x25C7F000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    t = sm64_saturn_gouraud_bank_alloc(&bank, &addr0);
    assert(t == &staging[0]);
    assert(addr0 == 0x25C7F000u);
    t = sm64_saturn_gouraud_bank_alloc(&bank, &addr1);
    assert(t == &staging[1]);
    assert(addr1 == 0x25C7F000u + sizeof(sm64_saturn_gouraud_table_t));
    assert(sm64_saturn_gouraud_bank_used_bytes(&bank) ==
           2 * sizeof(sm64_saturn_gouraud_table_t));
    sm64_saturn_gouraud_bank_begin(&bank); /* frame reset */
    assert(sm64_saturn_gouraud_bank_used_bytes(&bank) == 0);
}

static void test_gouraud_bank_overflow_returns_null(void)
{
    static sm64_saturn_gouraud_table_t staging[2];
    sm64_saturn_gouraud_bank_t bank;
    uintptr_t addr;

    assert(sm64_saturn_gouraud_bank_init(&bank, staging, 2, 0x1000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) != NULL);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) != NULL);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) == NULL);
    /* zero-capacity init (partition missing/overlapping) is legal and
     * yields an always-NULL bank -- the graceful all-flat fallback */
    assert(!sm64_saturn_gouraud_bank_init(&bank, staging, 0, 0x1000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) == NULL);
}
```

Register both in `main()`; add `#include "saturn_gouraud_bank.h"` at the top.

- [x] **Step 2: Run to confirm failure**

Expected: FAIL to compile — header missing.

- [x] **Step 3: Implement**

Create `src/port/saturn/gfx/saturn_gouraud_bank.h`:

```c
#ifndef SM64_SATURN_GOURAUD_BANK_H
#define SM64_SATURN_GOURAUD_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame-local VDP1 Gouraud table staging bank -- pure bookkeeping,
 * Yaul-free so the accounting is host-testable. One 8-byte table per
 * emitted primitive per frame (design spec 2026-07-24; per-frame
 * rebuild by user decision, caching is fidelity-ladder item 1).
 *
 * The Yaul-side owner (sourceboot) provides the staging array (which
 * MUST live in HWRAM, never .lwram_bss -- the SCU-DMA-from-LWRAM
 * lockup class documented in the VDP1 backend applies to this upload
 * path too) and the device base address (partitions.gouraud_base),
 * and performs the actual used-prefix upload after emission. Layout
 * matches Yaul's vdp1_gouraud_table_t (4 x RGB1555, 8 bytes,
 * libyaul .../vdp1/vram.h:32-34) -- static-asserted at the emit TU,
 * which sees both types. */
typedef struct sm64_saturn_gouraud_table {
    uint16_t colors[4];
} sm64_saturn_gouraud_table_t;

typedef struct sm64_saturn_gouraud_bank {
    sm64_saturn_gouraud_table_t *staging;
    uintptr_t vram_base;
    uint16_t capacity;
    uint16_t used;
} sm64_saturn_gouraud_bank_t;

/* Returns false (and configures an always-NULL bank) for capacity 0 --
 * the caller's graceful all-flat fallback when the VRAM partition is
 * missing or overlaps the command region. */
static inline bool
sm64_saturn_gouraud_bank_init(sm64_saturn_gouraud_bank_t *bank,
                              sm64_saturn_gouraud_table_t *staging,
                              uint16_t capacity, uintptr_t vram_base)
{
    bank->staging = staging;
    bank->vram_base = vram_base;
    bank->capacity = capacity;
    bank->used = 0;
    return capacity > 0;
}

static inline void
sm64_saturn_gouraud_bank_begin(sm64_saturn_gouraud_bank_t *bank)
{
    bank->used = 0;
}

/* Returns the staging slot to fill and writes the table's device
 * address (for CMDGRDA) to *vram_addr; NULL when exhausted. */
static inline sm64_saturn_gouraud_table_t *
sm64_saturn_gouraud_bank_alloc(sm64_saturn_gouraud_bank_t *bank,
                               uintptr_t *vram_addr)
{
    if (bank->used >= bank->capacity) {
        return NULL;
    }
    *vram_addr = bank->vram_base +
        (uintptr_t)bank->used * sizeof(sm64_saturn_gouraud_table_t);
    return &bank->staging[bank->used++];
}

static inline size_t
sm64_saturn_gouraud_bank_used_bytes(const sm64_saturn_gouraud_bank_t *bank)
{
    return (size_t)bank->used * sizeof(sm64_saturn_gouraud_table_t);
}

#endif
```

- [x] **Step 4: Run tests to verify pass**

Host suite: exit 0.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_gouraud_bank.h tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): frame-local Gouraud table staging bank (bookkeeping, host-tested)"
```

Committed as `a4ed0c4`. Byte-identical to the sketch; zero deviation.
Kept strictly to bookkeeping scope as instructed -- no Yaul includes,
no VRAM partition queries, no DMA queue code (independently grepped by
both reviewers, not just self-reported).

Two-stage review: spec-compliance ✅ clean, no issues -- independently
confirmed `_init` configures all fields (including `staging`/`vram_base`)
unconditionally before the `capacity > 0` return, so a zero-capacity
bank is fully well-formed rather than partially uninitialized; confirmed
the `>=` bounds check (not an off-by-one `>`); reproduced two mutations
(bounds-check flip, `_init` rewritten as an early-return before field
assignment) and confirmed both correctly break their target assertions;
ran the host suite with temporary execution markers to prove both new
tests actually run rather than being silently skipped.

Code-quality review: Ready to merge -- Yes, no Critical or Important
issues. Went further than the spec-compliance pass with 4 additional
independent mutations of its own (frozen address computation, hardcoded
`_used_bytes`, no-op `_begin`, non-advancing `_alloc`) -- 6/6 total
mutations caught across both reviews, 0% survival. Confirmed this file
correctly follows the codebase's Yaul-free bookkeeping-header idiom
(`saturn_light_q16.h`, `saturn_matrix_kernels.h`, `saturn_command_arena.h`)
rather than the OTHER sibling pattern that exists in this same directory
(`saturn_texture_residency.h`, which takes a Yaul `vdp1_vram_partitions_t*`
directly) -- confirmed as the deliberately correct choice for this task's
host-testability goal, not an oversight. One Minor, forward-looking-only
note (no fix needed now): Task 6 must remember to add the
`_Static_assert(sizeof(sm64_saturn_gouraud_table_t) ==
sizeof(vdp1_gouraud_table_t), ...)` at the emit TU as already sketched in
Task 6's own plan text below -- nothing on this task's side enforces that
Task 6 actually keeps it.

---

### Task 6: Gouraud emission + sourceboot wiring (Yaul side)

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h` (signature)
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile` (DMA queue TU, if absent)
- Modify: `docs/saturn/SLAVEDRIVER_ADAPTATION.md` (status line)

No host test covers this TU (it is not in the `verify-runtime-contracts` compile — Yaul-dependent); verification is compile + `make verify` + Task 7's live capture. Read every touched site before editing; the sketches below were verified against the tree at plan time but this file's conventions rule.

- [ ] **Step 1: Check whether sourceboot links the DMA queue**

```
grep -n "slavedriver_dma_queue\|gpl/" src/port/saturn/sourceboot/Makefile
```
If absent, add `$(ROOT)/src/port/saturn/gpl/slavedriver_dma_queue.c` to `SH_SRCS` (same list that gained `saturn_trig_q16.inc.c` in the matrix sprint). GPL isolation note: the queue stays in its clearly-GPL `gpl/` component, linked — not copied — exactly as `docs/saturn/SLAVEDRIVER_ADAPTATION.md` already authorizes (hwtest precedent). Update that doc's "Current status" section to say sourceboot now links the adapter for Gouraud-table uploads.

- [ ] **Step 2: Wire partitions + bank into sourceboot init**

In `src/port/saturn/sourceboot/main.c`, next to the existing backend init (`:149-160`), add (file-scope statics near `sourceboot_vdp1_cmdts`):

```c
#include "saturn_gouraud_bank.h"
#include "gpl/slavedriver_dma_queue.h" /* match the include style the
                                          file/Makefile actually use --
                                          check -I paths first */

/* HWRAM (.bss) deliberately: SCU DMA from LWRAM is the documented
 * lockup class the VDP1 backend already works around. 1536 * 8 =
 * 12,288 bytes against the ~191 KiB measured HWRAM margin. */
static sm64_saturn_gouraud_table_t
    sourceboot_gouraud_staging[SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
static sm64_saturn_gouraud_bank_t sourceboot_gouraud_bank;
```

and in the init sequence, after the backend init succeeds:

```c
    {
        vdp1_vram_partitions_t partitions;
        uintptr_t cmd_end = (uintptr_t)VDP1_VRAM(0) +
            (uintptr_t)SOURCEBOOT_VDP1_COMMAND_CAPACITY *
                sizeof(vdp1_cmdt_t);
        uint16_t capacity = 0;

        saturn_dma_queue_init();
        vdp1_vram_partitions_get(&partitions);
        /* The backend CPU-copies its command list to VDP1_VRAM(0)
         * without consulting Yaul's partition layout -- verify the
         * gouraud partition clears the command region before trusting
         * it. Overlap => capacity 0 => every triangle takes the
         * counted flat fallback (degradation contract), no crash. */
        if ((uintptr_t)partitions.gouraud_base >= cmd_end &&
            partitions.gouraud_size >=
                sizeof(sm64_saturn_gouraud_table_t)) {
            uint32_t fit = partitions.gouraud_size /
                sizeof(sm64_saturn_gouraud_table_t);
            capacity = (uint16_t)(fit >
                SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES ?
                SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES : fit);
        } else {
            dbgio_puts("sourceboot: gouraud partition unusable\n");
        }
        (void)sm64_saturn_gouraud_bank_init(&sourceboot_gouraud_bank,
            sourceboot_gouraud_staging, capacity,
            (uintptr_t)partitions.gouraud_base);
    }
```

Update the per-frame call (`:175-176`) to pass the bank (new signature, Step 3).

- [ ] **Step 3: Gouraud emission**

`saturn_fast3d_vdp1_emit.h`: signature becomes

```c
void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend,
                                  sm64_saturn_gouraud_bank_t *gouraud_bank);
```
(add the bank header include). In the `.c`:

```c
#include "saturn_gouraud.h"
#include "saturn_gouraud_bank.h"
#include "gpl/slavedriver_dma_queue.h"

_Static_assert(sizeof(sm64_saturn_gouraud_table_t) ==
               sizeof(vdp1_gouraud_table_t),
               "staging table must match Yaul's VDP1 layout");
```

At the top of the function: `sm64_saturn_gouraud_bank_begin(gouraud_bank);`. Replace the per-triangle command body (`:44-52`):

```c
            uintptr_t grda_addr;
            sm64_saturn_gouraud_table_t *table =
                sm64_saturn_gouraud_bank_alloc(gouraud_bank, &grda_addr);

            vdp1_cmdt_polygon_set(cmdt);
            if (table != NULL) {
                /* Table entries ARE the final corner colors: with the
                 * neutral base (0xC210, R=G=B=16) VDP1's signed
                 * correction makes 16 + (entry - 16) == entry
                 * (saturn_gouraud.h). Corner D duplicates C, matching
                 * the (i0,i1,i2,i2) degenerate-quad convention. */
                table->colors[0] = tri->corner_rgb1555[0];
                table->colors[1] = tri->corner_rgb1555[1];
                table->colors[2] = tri->corner_rgb1555[2];
                table->colors[3] = tri->corner_rgb1555[2];
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_GOURAUD
                });
                vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                    .raw = sm64_saturn_gouraud_neutral_color()
                });
                vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)grda_addr);
            } else {
                /* Bank exhausted or partition unusable: counted flat
                 * fallback (degradation contract). */
                profile->gouraud_bank_overflow++;
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_REPLACE
                });
                vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                    .raw = tri->corner_rgb1555[0]
                });
            }
            vdp1_cmdt_vtx_set(cmdt, quad_vertices);
            profile->triangles_vdp1_emitted++;
```

After the bucket loops, BEFORE `sm64_saturn_vdp1_backend_finish/upload` (tables must be in VRAM before VDP1 draws them):

```c
    if (sm64_saturn_gouraud_bank_used_bytes(gouraud_bank) > 0) {
        /* Used-prefix only (the E0 command-arena lesson): synchronous
         * for bring-up, same as the backend's own upload. */
        saturn_dma_queue_transfer_wait(
            (void *)gouraud_bank->vram_base, gouraud_bank->staging,
            sm64_saturn_gouraud_bank_used_bytes(gouraud_bank),
            SATURN_DMA_QUEUE_SCU);
    }
```

- [ ] **Step 4: Cross-compile + verify**

Forced clean rebuild (delete the `@`-mangled objects for `saturn_fast3d_vdp1_emit.c`, `saturn_fast3d_frontend.c`, `main.c`, plus ELF/ISO/CUE), then `make -j2 && make verify` in `src/port/saturn/sourceboot` (msys64 pattern). Expected: both exit 0, zero warnings. Also re-run the host suite — it must still pass untouched.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c src/port/saturn/sourceboot/main.c src/port/saturn/sourceboot/Makefile docs/saturn/SLAVEDRIVER_ADAPTATION.md
git commit -m "feat(saturn): VDP1 Gouraud emission -- per-triangle tables, used-prefix DMA upload"
```

---

### Task 7: Full regression + live capture (the payoff)

**Files:** none (verification; produces evidence).

- [ ] **Step 1: Full regression**

All four host targets (`verify-tools`, `verify-runtime-contracts`, `verify-mtxq-ctors`, `verify-mtxf-lookat-host-diff`) exit 0; SH-2 cross-compile + `make verify` exit 0.

- [ ] **Step 2: Capture**

Standing discipline: fresh `sh-elf-nm` resolve of `_sourceboot_fast3d` (address WILL have shifted), and **re-ground-truth the profile-struct offsets via a fresh `offsetof()` probe compile** — this plan appended 5 counters to the struct; never hand-derive. Then the proven recipe (paths verified in the v0.1.0 capture: ymir at `D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe`, BIOS at `C:\Users\estee\AppData\Local\Temp\Sega Saturn BIOS (USA).bin`):

```
./.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_hwtest.py \
  --ymir "<ymir path>" --ipl "<bios path>" \
  --game "build/saturn/sourceboot/e2-bob/sm64-saturn-sourceboot-e2.cue" \
  --dram-cart --bios-input --frames 240 --handoff-yield \
  --post-poke-frames 25000 --probe-address <fresh> --probe-count 240 \
  --allow-invalid --timeout 580 \
  --screenshot-output docs/saturn/evidence/screenshots/e2-sourceboot-gouraud-freeroam-2026-07-24.png \
  --output docs/saturn/evidence/reports/e2-sourceboot-gouraud-freeroam-2026-07-24.json
```
(`--probe-count 240`: the struct grew by 20 bytes; size it from the fresh `offsetof` probe's `sizeof` output plus margin. `--timeout 580` per the v0.1.0 measurement.)

- [ ] **Step 3: Judge against concrete criteria**

| Metric | v0.1.0 baseline | Success threshold |
| --- | --- | --- |
| `lit_vertices` | (field didn't exist) | > 0, dominating `unlit_vertices` |
| `unsupported_num_lights` | — | small or 0 |
| `fog_dropped_triangles` | — | > 0 (BOB terrain uses fog — proves the gate runs) |
| `gouraud_bank_overflow` | — | 0 (180 triangles ≪ 1536 tables) |
| `triangles_vdp1_emitted` | 0 at sampled frame | ≥ baseline behavior |
| Screenshot | garbage-colored patchwork (normals as RGB) | real material colors, smooth shading gradients |

Report raw numbers honestly. Both outcomes acceptable: (a) recognizable shaded BOB — present to the user; (b) counters good but visuals wrong — the counters attribute the next lead; commit the evidence either way. **Do not** write TIMELINE/gallery entries — the user's visual confirmation gates that, per standing rule.

- [ ] **Step 4: Evidence commit**

```bash
git add docs/saturn/evidence/reports/e2-sourceboot-gouraud-freeroam-2026-07-24.json \
        docs/saturn/evidence/screenshots/e2-sourceboot-gouraud-freeroam-2026-07-24.png
git commit -m "test(saturn): Gouraud free-roam capture evidence"
```
Present the screenshot to the user.

---

## Out of scope (from the spec — repeated so no task invents them)

Texture decode/bake; fog rendering (ladder 2); table caching (ladder 1); `rendering_graph_node.c` (untouched this cycle); audio. The fidelity ladder lives in the spec — do not build any of it speculatively.

## Self-review (performed at write time)

- **Spec coverage**: light decode (T2), lit/unlit gating + bug fix (T3), Q16 evaluator + differential test (T1), corner colors (T4), bank + used-prefix upload (T5/T6), CC_GOURAUD emission + neutral base + A/B/C/C (T6), all five degradation counters (T2/T3/T4/T6), partition-overlap graceful fallback (T6), live capture + user gate (T7). No spec requirement unassigned.
- **Placeholder scan**: the Task 1 fixture's `Lights1` values are explicitly flagged as shape-only with the instruction to substitute the real BOB values read in Step 1 — deliberate read-the-real-data instruction, not a placeholder.
- **Type consistency**: `sm64_saturn_light_state_t` fields (`dir_col/dir_dir/amb_col/num_lights/lights_changed/coeff_q16`) match across T1 decl, T2 stores, T3 eval; `corner_rgb1555[3]` consistent T4→T6; bank API names consistent T5→T6; emit signature change propagated to sourceboot main.c in T6 Step 2.
- **Known risks, called out inline**: G_MOVEWORD field positions and the transpose orientation are both flagged as verify-against-gfx_pc-before-trusting; the partition-overlap check exists precisely because sourceboot's backend bypasses Yaul's partition layout; SCU-DMA-from-LWRAM lockup is guarded by the HWRAM staging placement note.
