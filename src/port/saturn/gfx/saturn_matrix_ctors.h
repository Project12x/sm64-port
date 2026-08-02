#ifndef SM64_SATURN_MATRIX_CTORS_H
#define SM64_SATURN_MATRIX_CTORS_H

#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"
#include "saturn_render_native_math.h"

/* Q16.16 constructors mirroring src/engine/math_util.c's mtxf_* matrix
 * builders, formula-for-formula (same row/column conventions, same sign
 * choices, same output layout), in pure integer arithmetic. See
 * saturn_matrix_kernels.h's header comment for why (toolchain soft-float
 * proven broken) and the SGL/Z-Treme/SlaveDriver precedent for this
 * being the architecturally correct Saturn approach.
 *
 * UNIT ANALYSIS (load-bearing -- do not change casually):
 * - Positions/translations arrive as Q16.16 (+-32767 world-unit range,
 *   same documented assumption as saturn_matrix.h's decode).
 * - mtxq_lookat reduces position deltas to INTEGER world units (>>16)
 *   before squaring: BOB-scale deltas (up to ~60000 units) square to
 *   ~3.6e9 and sum x3 within int64 trivially, whereas squaring raw
 *   Q16.16 deltas ((60000<<16)^2 * 3 = 1.16e19) would overflow int64
 *   (max 9.2e18). Direction = (Q16.16 delta) / (integer magnitude)
 *   yields a Q16.16 unit component directly, one 64/32 divide per
 *   component. Normalization computes one Q16 reciprocal through the
 *   libyaul-backed SH-2 DIVU seam, then reuses it for all components.
 * - Sub-unit truncation from the >>16 delta reduction shifts direction
 *   by at most 1 part in the magnitude (irrelevant at world scale;
 *   documented, accepted for bring-up). */

/* Normalize a Q16.16 3-vector in place. Squares of unit-range Q16
 * components are Q32 (fit int64 with huge margin summed x3);
 * isqrt64(Q32) = Q16 magnitude; component/(magnitude) rescaled by <<16
 * keeps Q16.16. Zero vector: left unchanged (caller guards). */
static inline void sm64_saturn_q16_vec3_normalize(int32_t v[3])
{
    int64_t mag2 = (int64_t) v[0] * v[0] + (int64_t) v[1] * v[1]
                 + (int64_t) v[2] * v[2];
    int64_t mag = sm64_saturn_isqrt64(mag2); /* Q16 */
    int32_t reciprocal_q16;
    if (mag == 0) {
        return;
    }
    if (mag > INT32_MAX
        || !sm64_saturn_div_s64_s32(INT64_C(1) << 32, (int32_t) mag,
                                     &reciprocal_q16)) {
        return;
    }
#if defined(SM64_SATURN_TEST_MUTATE_NORMALIZE_RECIPROCAL)
    reciprocal_q16 ^= 1;
#endif
    v[0] = sm64_saturn_q16_mul(v[0], reciprocal_q16);
    v[1] = sm64_saturn_q16_mul(v[1], reciprocal_q16);
    v[2] = sm64_saturn_q16_mul(v[2], reciprocal_q16);
}

/* Graph projection constructors. Inputs are lowered once at the graph-node
 * boundary; these mirror guPerspectiveF/guOrthoF's scale==1 call sites and
 * use the same row-major layout consumed by the Saturn Fast3D frontend.
 * Invalid/singular inputs produce identity and return false, keeping the
 * target path deterministic without falling back into float math. */
static inline bool
sm64_saturn_mtxq_perspective(sm64_saturn_mtx_t *dest, uint16_t *persp_norm,
                             int32_t fovy_degrees_q16, int32_t aspect_q16,
                             int16_t near, int16_t far)
{
    int32_t cotangent_q16;
    int32_t xscale_q16;
    int32_t depth_scale_q16;
    int32_t depth_translate_q16;
    const int32_t half_fovy_angle = (fovy_degrees_q16 / 360) / 2;
    const int32_t sine_q16 = sm64_saturn_sins_q16(half_fovy_angle);
    const int32_t cosine_q16 = sm64_saturn_coss_q16(half_fovy_angle);
    const int32_t depth_denominator = (int32_t) near - (int32_t) far;
    const int32_t depth_sum = (int32_t) near + (int32_t) far;

    if (persp_norm != NULL) {
        *persp_norm = UINT16_MAX;
    }
    sm64_saturn_matrix_identity(dest);
    if (sine_q16 == 0 || aspect_q16 == 0 || depth_denominator == 0
        || !sm64_saturn_div_s64_s32((int64_t) cosine_q16 << 16,
                                     sine_q16, &cotangent_q16)
        || !sm64_saturn_div_s64_s32((int64_t) cotangent_q16 << 16,
                                     aspect_q16, &xscale_q16)
        || !sm64_saturn_div_s64_s32((int64_t) depth_sum << 16,
                                     depth_denominator, &depth_scale_q16)
        || !sm64_saturn_div_s64_s32(
               ((int64_t) 2 * near * far) << 16,
               depth_denominator, &depth_translate_q16)) {
        return false;
    }

    dest->m[0][0] = xscale_q16;
    dest->m[1][1] = cotangent_q16;
    dest->m[2][2] = depth_scale_q16;
    dest->m[2][3] = -(1 << 16);
    dest->m[3][2] = depth_translate_q16;
    dest->m[3][3] = 0;

    if (persp_norm != NULL) {
        if (depth_sum <= 2) {
            *persp_norm = UINT16_MAX;
        } else {
            int32_t norm = 131072 / depth_sum;
            *persp_norm = (uint16_t) (norm > 0 ? norm : 1);
        }
    }
    return true;
}

static inline bool
sm64_saturn_mtxq_ortho(sm64_saturn_mtx_t *dest,
                       int32_t left_q16, int32_t right_q16,
                       int32_t bottom_q16, int32_t top_q16,
                       int32_t near_q16, int32_t far_q16)
{
    const int64_t width = (int64_t) right_q16 - left_q16;
    const int64_t height = (int64_t) top_q16 - bottom_q16;
    const int64_t depth = (int64_t) far_q16 - near_q16;
    int32_t sx, sy, sz, tx, ty, tz;

    sm64_saturn_matrix_identity(dest);
    if (width == 0 || height == 0 || depth == 0
        || width > INT32_MAX || width < INT32_MIN
        || height > INT32_MAX || height < INT32_MIN
        || depth > INT32_MAX || depth < INT32_MIN
        || !sm64_saturn_div_s64_s32(INT64_C(2) << 32,
                                     (int32_t) width, &sx)
        || !sm64_saturn_div_s64_s32(INT64_C(2) << 32,
                                     (int32_t) height, &sy)
        || !sm64_saturn_div_s64_s32(-(INT64_C(2) << 32),
                                     (int32_t) depth, &sz)
        || !sm64_saturn_div_s64_s32(
               -((int64_t) right_q16 + left_q16) * (INT64_C(1) << 16),
               (int32_t) width, &tx)
        || !sm64_saturn_div_s64_s32(
               -((int64_t) top_q16 + bottom_q16) * (INT64_C(1) << 16),
               (int32_t) height, &ty)
        || !sm64_saturn_div_s64_s32(
               -((int64_t) far_q16 + near_q16) * (INT64_C(1) << 16),
               (int32_t) depth, &tz)) {
        return false;
    }

    dest->m[0][0] = sx;
    dest->m[1][1] = sy;
    dest->m[2][2] = sz;
    dest->m[3][0] = tx;
    dest->m[3][1] = ty;
    dest->m[3][2] = tz;
    return true;
}

/* Mirrors mtxf_lookat (math_util.c:194-266) including its exact
 * negative-reciprocal convention (colZ points from->to NEGATED) and
 * roll handling. from/to are Q16.16 world positions; roll is the same
 * s16 angle the engine uses. */
static inline void
sm64_saturn_mtxq_lookat(sm64_saturn_mtx_t *mtx, const int32_t from[3],
                        const int32_t to[3], int16_t roll)
{
    int32_t colX[3], colY[3], colZ[3];
    int64_t dxi, dzi, mag;
    /* from/to individually satisfy this port's documented +-32767
     * world-unit ceiling (saturn_matrix.h's decode note), but their
     * DIFFERENCE does not: two independent values each near the
     * ceiling can differ by up to ~65534 real units, and that
     * magnitude's Q16.16 representation (~4.29e9 raw) does not fit
     * int32_t (max ~2.147e9) -- confirmed with a compiled repro
     * (from=-20000, to=15000 on one axis: to[0]-from[0] as plain
     * int32_t silently wraps to a wrong-sign value, corrupting every
     * downstream rotation-column entry). Widen to int64_t before
     * subtracting; narrow back to int32_t only after dividing by mag,
     * once the result is unit-range and provably safe -- same
     * widen-before-combine pattern saturn_matrix.h's
     * sm64_saturn_matrix_mul already uses for the same class of
     * problem. */
    int64_t dx_wide = (int64_t) to[0] - (int64_t) from[0];
    int64_t dz_wide = (int64_t) to[2] - (int64_t) from[2];
    int32_t dx_q, dz_q;

    /* horizontal direction, integer-unit magnitude (see unit analysis) */
    dxi = dx_wide >> 16;
    dzi = dz_wide >> 16;
    mag = sm64_saturn_isqrt64(dxi * dxi + dzi * dzi);
    if (mag == 0) {
        mag = 1;
    }
    /* float code: d *= -1/len. Negated Q16.16 unit components: */
    (void) sm64_saturn_div_s64_s32(-dx_wide, (int32_t) mag, &dx_q);
    (void) sm64_saturn_div_s64_s32(-dz_wide, (int32_t) mag, &dz_q);

    colY[0] = sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dz_q);
    colY[1] = sm64_saturn_coss_q16(roll);
    colY[2] = -sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dx_q);

    /* full look direction, same integer-unit reduction, negated --
     * same int64_t-widened delta as above, same overflow reasoning */
    {
        int64_t vx = (int64_t) to[0] - (int64_t) from[0];
        int64_t vy = (int64_t) to[1] - (int64_t) from[1];
        int64_t vz = (int64_t) to[2] - (int64_t) from[2];
        int64_t xi = vx >> 16, yi = vy >> 16, zi = vz >> 16;
        mag = sm64_saturn_isqrt64(xi * xi + yi * yi + zi * zi);
        if (mag == 0) {
            mag = 1;
        }
        (void) sm64_saturn_div_s64_s32(-vx, (int32_t) mag, &colZ[0]);
        (void) sm64_saturn_div_s64_s32(-vy, (int32_t) mag, &colZ[1]);
        (void) sm64_saturn_div_s64_s32(-vz, (int32_t) mag, &colZ[2]);
    }

    /* colX = colY x colZ; renormalize (float code divides by +len) */
    colX[0] = sm64_saturn_q16_mul(colY[1], colZ[2])
            - sm64_saturn_q16_mul(colY[2], colZ[1]);
    colX[1] = sm64_saturn_q16_mul(colY[2], colZ[0])
            - sm64_saturn_q16_mul(colY[0], colZ[2]);
    colX[2] = sm64_saturn_q16_mul(colY[0], colZ[1])
            - sm64_saturn_q16_mul(colY[1], colZ[0]);
    sm64_saturn_q16_vec3_normalize(colX);

    /* colY = colZ x colX; renormalize (matches float code's final
     * recompute -- order matters, keep it) */
    colY[0] = sm64_saturn_q16_mul(colZ[1], colX[2])
            - sm64_saturn_q16_mul(colZ[2], colX[1]);
    colY[1] = sm64_saturn_q16_mul(colZ[2], colX[0])
            - sm64_saturn_q16_mul(colZ[0], colX[2]);
    colY[2] = sm64_saturn_q16_mul(colZ[0], colX[1])
            - sm64_saturn_q16_mul(colZ[1], colX[0]);
    sm64_saturn_q16_vec3_normalize(colY);

    /* write in the float code's exact layout: column c of the rotation
     * lives at m[row][c] with rows = x/y/z components */
    mtx->m[0][0] = colX[0]; mtx->m[1][0] = colX[1]; mtx->m[2][0] = colX[2];
    mtx->m[0][1] = colY[0]; mtx->m[1][1] = colY[1]; mtx->m[2][1] = colY[2];
    mtx->m[0][2] = colZ[0]; mtx->m[1][2] = colZ[1]; mtx->m[2][2] = colZ[2];

    /* translation row: -(from . col) per column, 64-bit dot products */
    for (int c = 0; c < 3; c++) {
        int64_t dot = (int64_t) from[0] * mtx->m[0][c]
                    + (int64_t) from[1] * mtx->m[1][c]
                    + (int64_t) from[2] * mtx->m[2][c];
        mtx->m[3][c] = (int32_t) (-(dot >> 16));
    }
    mtx->m[0][3] = 0;
    mtx->m[1][3] = 0;
    mtx->m[2][3] = 0;
    mtx->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_rotate_zxy_and_translate (math_util.c:272-299). */
static inline void
sm64_saturn_mtxq_rotate_zxy_and_translate(sm64_saturn_mtx_t *dest,
                                          const int32_t translate[3],
                                          int16_t rx, int16_t ry, int16_t rz)
{
    const int32_t sx = sm64_saturn_sins_q16(rx), cx = sm64_saturn_coss_q16(rx);
    const int32_t sy = sm64_saturn_sins_q16(ry), cy = sm64_saturn_coss_q16(ry);
    const int32_t sz = sm64_saturn_sins_q16(rz), cz = sm64_saturn_coss_q16(rz);
    const int32_t sxsy = sm64_saturn_q16_mul(sx, sy);
    const int32_t sxcy = sm64_saturn_q16_mul(sx, cy);

    dest->m[0][0] = sm64_saturn_q16_mul(cy, cz)
                  + sm64_saturn_q16_mul(sxsy, sz);
    dest->m[1][0] = -sm64_saturn_q16_mul(cy, sz)
                  + sm64_saturn_q16_mul(sxsy, cz);
    dest->m[2][0] = sm64_saturn_q16_mul(cx, sy);
    dest->m[3][0] = translate[0];

    dest->m[0][1] = sm64_saturn_q16_mul(cx, sz);
    dest->m[1][1] = sm64_saturn_q16_mul(cx, cz);
    dest->m[2][1] = -sx;
    dest->m[3][1] = translate[1];

    dest->m[0][2] = -sm64_saturn_q16_mul(sy, cz)
                  + sm64_saturn_q16_mul(sxcy, sz);
    dest->m[1][2] = sm64_saturn_q16_mul(sy, sz)
                  + sm64_saturn_q16_mul(sxcy, cz);
    dest->m[2][2] = sm64_saturn_q16_mul(cx, cy);
    dest->m[3][2] = translate[2];

    dest->m[0][3] = dest->m[1][3] = dest->m[2][3] = 0;
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_rotate_xyz_and_translate (math_util.c:305-334). */
static inline void
sm64_saturn_mtxq_rotate_xyz_and_translate(sm64_saturn_mtx_t *dest,
                                          const int32_t b[3],
                                          int16_t rx, int16_t ry, int16_t rz)
{
    const int32_t sx = sm64_saturn_sins_q16(rx), cx = sm64_saturn_coss_q16(rx);
    const int32_t sy = sm64_saturn_sins_q16(ry), cy = sm64_saturn_coss_q16(ry);
    const int32_t sz = sm64_saturn_sins_q16(rz), cz = sm64_saturn_coss_q16(rz);
    const int32_t sxsy = sm64_saturn_q16_mul(sx, sy);
    const int32_t cxsy = sm64_saturn_q16_mul(cx, sy);

    dest->m[0][0] = sm64_saturn_q16_mul(cy, cz);
    dest->m[0][1] = sm64_saturn_q16_mul(cy, sz);
    dest->m[0][2] = -sy;
    dest->m[0][3] = 0;

    dest->m[1][0] = sm64_saturn_q16_mul(sxsy, cz)
                  - sm64_saturn_q16_mul(cx, sz);
    dest->m[1][1] = sm64_saturn_q16_mul(sxsy, sz)
                  + sm64_saturn_q16_mul(cx, cz);
    dest->m[1][2] = sm64_saturn_q16_mul(sx, cy);
    dest->m[1][3] = 0;

    dest->m[2][0] = sm64_saturn_q16_mul(cxsy, cz)
                  + sm64_saturn_q16_mul(sx, sz);
    dest->m[2][1] = sm64_saturn_q16_mul(cxsy, sz)
                  - sm64_saturn_q16_mul(sx, cz);
    dest->m[2][2] = sm64_saturn_q16_mul(cx, cy);
    dest->m[2][3] = 0;

    dest->m[3][0] = b[0];
    dest->m[3][1] = b[1];
    dest->m[3][2] = b[2];
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_billboard (math_util.c:342-365): camera-facing rotation
 * plus the object position transformed through the camera matrix. */
static inline void
sm64_saturn_mtxq_billboard(sm64_saturn_mtx_t *dest,
                           const sm64_saturn_mtx_t *mtx,
                           const int32_t position[3], int16_t angle)
{
    dest->m[0][0] = sm64_saturn_coss_q16(angle);
    dest->m[0][1] = sm64_saturn_sins_q16(angle);
    dest->m[0][2] = 0;
    dest->m[0][3] = 0;

    dest->m[1][0] = -dest->m[0][1];
    dest->m[1][1] = dest->m[0][0];
    dest->m[1][2] = 0;
    dest->m[1][3] = 0;

    dest->m[2][0] = 0;
    dest->m[2][1] = 0;
    dest->m[2][2] = 1 << 16;
    dest->m[2][3] = 0;

    for (int c = 0; c < 3; c++) {
        int64_t dot = (int64_t) mtx->m[0][c] * position[0]
                    + (int64_t) mtx->m[1][c] * position[1]
                    + (int64_t) mtx->m[2][c] * position[2];
        /* (dot>>16) and mtx->m[3][c] are each independently bounded
         * only by this port's +-32767 world-unit ceiling (the rotation
         * columns are unit-range, but the translation row and
         * "position" are not) -- same overflow class as
         * sm64_saturn_mtxq_lookat's delta computation above, just via
         * `+` instead of `-`. Combine in int64_t and narrow once. */
        int64_t sum = (dot >> 16) + (int64_t) mtx->m[3][c];
        dest->m[3][c] = (int32_t) sum;
    }
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_scale_vec3f (math_util.c:538-547).
 *
 * Overflow note: sm64_saturn_q16_mul's documented safe range (|a|,|b| <=
 * 1<<16) is NOT guaranteed here -- mtx's entries and s are both general
 * Q16.16 values, not sin/cos-bounded. In-tree scale usage observed at
 * rendering_graph_node.c:426,454,457,825,903 runs ~0.1x-9x, far from the
 * format's ceiling; accepted as a documented assumption for bring-up,
 * not a proven bound. If a future caller pushes scale or matrix-entry
 * magnitude toward the +-32767 ceiling simultaneously, this needs an
 * overflow-checked multiply. */
static inline void
sm64_saturn_mtxq_scale_vec3f(sm64_saturn_mtx_t *dest,
                             const sm64_saturn_mtx_t *mtx,
                             const int32_t s[3])
{
    for (int i = 0; i < 4; i++) {
        dest->m[0][i] = sm64_saturn_q16_mul(mtx->m[0][i], s[0]);
        dest->m[1][i] = sm64_saturn_q16_mul(mtx->m[1][i], s[1]);
        dest->m[2][i] = sm64_saturn_q16_mul(mtx->m[2][i], s[2]);
        dest->m[3][i] = mtx->m[3][i];
    }
}

/* Mirrors mtxf_translate (math_util.c:181-192): identity + position. */
static inline void
sm64_saturn_mtxq_translate(sm64_saturn_mtx_t *dest, const int32_t b[3])
{
    sm64_saturn_matrix_identity(dest);
    dest->m[3][0] = b[0];
    dest->m[3][1] = b[1];
    dest->m[3][2] = b[2];
}

/* Mirrors mtxf_rotate_xy (math_util.c:596+): Z-axis screen roll used by
 * the HUD/ortho camera node. Writes a full matrix; the caller converts
 * to the wire Mtx exactly like every other constructor. */
static inline void
sm64_saturn_mtxq_rotate_xy(sm64_saturn_mtx_t *dest, int16_t angle)
{
    sm64_saturn_matrix_identity(dest);
    dest->m[0][0] = sm64_saturn_coss_q16(angle);
    dest->m[0][1] = sm64_saturn_sins_q16(angle);
    dest->m[1][0] = -dest->m[0][1];
    dest->m[1][1] = dest->m[0][0];
}

#endif
