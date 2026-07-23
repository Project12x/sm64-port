#ifndef SM64_SATURN_MATRIX_CTORS_H
#define SM64_SATURN_MATRIX_CTORS_H

#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"

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
 *   component (libgcc __divdi3 -- exact integer, not soft-float).
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
    if (mag == 0) {
        return;
    }
    v[0] = (int32_t) (((int64_t) v[0] << 16) / mag);
    v[1] = (int32_t) (((int64_t) v[1] << 16) / mag);
    v[2] = (int32_t) (((int64_t) v[2] << 16) / mag);
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
    int32_t dx_q = to[0] - from[0];
    int32_t dz_q = to[2] - from[2];

    /* horizontal direction, integer-unit magnitude (see unit analysis) */
    dxi = (int64_t) (dx_q >> 16);
    dzi = (int64_t) (dz_q >> 16);
    mag = sm64_saturn_isqrt64(dxi * dxi + dzi * dzi);
    if (mag == 0) {
        mag = 1;
    }
    /* float code: d *= -1/len. Negated Q16.16 unit components: */
    dx_q = (int32_t) (-((int64_t) dx_q) / mag);
    dz_q = (int32_t) (-((int64_t) dz_q) / mag);

    colY[0] = sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dz_q);
    colY[1] = sm64_saturn_coss_q16(roll);
    colY[2] = -sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dx_q);

    /* full look direction, same integer-unit reduction, negated */
    {
        int32_t vx = to[0] - from[0];
        int32_t vy = to[1] - from[1];
        int32_t vz = to[2] - from[2];
        int64_t xi = vx >> 16, yi = vy >> 16, zi = vz >> 16;
        mag = sm64_saturn_isqrt64(xi * xi + yi * yi + zi * zi);
        if (mag == 0) {
            mag = 1;
        }
        colZ[0] = (int32_t) (-((int64_t) vx) / mag);
        colZ[1] = (int32_t) (-((int64_t) vy) / mag);
        colZ[2] = (int32_t) (-((int64_t) vz) / mag);
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
        dest->m[3][c] = (int32_t) (dot >> 16) + mtx->m[3][c];
    }
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_scale_vec3f (math_util.c:538-547). */
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
