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
 * TRANSPOSE ORIENTATION -- verified directly against the real
 * gfx_transposed_matrix_mul (gfx_pc.c:528-532), which an earlier plan
 * draft of this header had backwards (M[j][i] instead of M[i][j]). The
 * real helper is:
 *   res[0] = a[0]*b[0][0] + a[1]*b[0][1] + a[2]*b[0][2];
 *   res[1] = a[0]*b[1][0] + a[1]*b[1][1] + a[2]*b[1][2];
 *   res[2] = a[0]*b[2][0] + a[1]*b[2][1] + a[2]*b[2][2];
 * i.e. res[i] = sum_j a[j]*b[i][j] -- the light direction dotted with
 * ROW i of the modelview matrix, so coeffs[i] = sum_j light_dir[j] *
 * M[i][j]. It is called "transposed" relative to gfx_sp_vertex's own
 * vertex-POSITION transform convention in the SAME file (x_i =
 * sum_j v[j]*MP[j][i], the opposite index order) -- not relative to how
 * the modelview matrix itself is stored. sm64_saturn_mtx_t.m[row][col]
 * mirrors gfx_pc.c's float matrix[row][col] one-to-one, with no
 * additional swap introduced by this port's own representation:
 * saturn_matrix.h's sm64_saturn_matrix_mul matches gfx_matrix_mul
 * index-for-index, and saturn_matrix_ctors.h's mtxq_lookat writes "in
 * the float code's exact layout". So this evaluator uses
 * modelview_top->m[i][j] (row i, column j) below, matching gfx_pc.c
 * directly -- not m[j][i].
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
 * normal) is transformed by gfx_transposed_matrix_mul --
 * coeff[i] = sum_j M[i][j] * dir[j] -- then normalized (see the header
 * comment above for the verified index order). The /127 folds gfx_pc's
 * dir[]/127.0f pre-scale; normalization makes any residual scale
 * irrelevant.
 *
 * Overflow safety: the largest possible |m[i][j]| is INT32_MAX
 * (~2^31, this port's documented Q16.16 ceiling -- also exactly what
 * sm64_saturn_float_to_q16's saturation path produces for any
 * out-of-range float, so this is a realistic input, not a contrived
 * one), |dir_dir[j]| <= 128 (true int8_t extreme). Three terms
 * summed before the /127: |sum| < 3 * 2^31 * 128 < 2^40, comfortably
 * inside int64_t (max ~2^63) with huge margin -- so the raw dot
 * product AND the /127 result both stay exact in int64_t. The
 * subsequent bound-to-<=1<<20 halving loop operates entirely in
 * int64_t (its own abs-value computation is therefore safe: int64_t
 * can represent the negation of any value this sum can reach, unlike
 * the previous int32_t version, which hit real signed-overflow UB --
 * and a real bypass, not just UB in the abstract -- for a modelview
 * entry at exactly INT32_MIN, confirmed by compiling and running the
 * case). Only narrow to int32_t once every component is provably
 * bounded, for vec3_normalize's squared-sum contract
 * (saturn_matrix_ctors.h's unit-range assumption; 1<<20 leaves
 * >1000x headroom under that function's own int64 mag2 overflow
 * boundary, not just barely enough). */
static inline void
sm64_saturn_light_recompute_coeffs(sm64_saturn_light_state_t *st,
                                   const sm64_saturn_mtx_t *modelview_top)
{
    int64_t sum[3];
    int32_t v[3];

    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int j = 0; j < 3; j++) {
            s += (int64_t)modelview_top->m[i][j]
               * (int64_t)st->dir_dir[j];
        }
        sum[i] = s / 127;
    }
    for (;;) {
        int64_t a0 = sum[0] < 0 ? -sum[0] : sum[0];
        int64_t a1 = sum[1] < 0 ? -sum[1] : sum[1];
        int64_t a2 = sum[2] < 0 ? -sum[2] : sum[2];
        if (a0 <= (1 << 20) && a1 <= (1 << 20) && a2 <= (1 << 20)) {
            break;
        }
        sum[0] /= 2;
        sum[1] /= 2;
        sum[2] /= 2;
    }
    v[0] = (int32_t)sum[0];
    v[1] = (int32_t)sum[1];
    v[2] = (int32_t)sum[2];
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
