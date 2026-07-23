/* Host differential test: Q16.16 render-matrix constructors vs. the real
 * float originals in src/engine/math_util.c, compiled natively.
 *
 * Tolerance model: constructors' rotation entries are unit-range; the
 * Q16.16 kernels use the trig table converted from the same f32 data, so
 * agreement should be within a few Q16 ulps for trig-driven entries.
 * Translation entries are exact conversions. Lookat involves
 * normalization (integer sqrt + 64-bit divide vs. float 1/sqrtf), so its
 * unit-vector entries get a wider documented tolerance. Every tolerance
 * is asserted, not just printed -- a regression fails the suite. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "engine/math_util.h"

#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"
#include "saturn_matrix_ctors.h"

/*
 * math_util.c compiles as a single translation unit, and the linker pulls
 * it in at object-file granularity: once ANY symbol from it is needed,
 * every other function's undefined references become link requirements
 * too -- even functions this test never calls. This was ground-truthed by
 * attempting the link, not assumed: the same 3 externals that
 * tools/saturn/mtxf_lookat_host_diff_test.c already needed turned out to
 * be exactly sufficient here too, even though this test additionally
 * calls mtxf_rotate_zxy_and_translate, mtxf_rotate_xyz_and_translate,
 * mtxf_billboard, mtxf_scale_vec3f, mtxf_translate, and mtxf_rotate_xy --
 * none of those touch find_floor/guMtxF2L/gVec3fZero, so the set of
 * required stubs does not grow. Real signatures, ground-truthed rather
 * than guessed:
 *   - find_floor: src/engine/surface_collision.h:44
 *   - guMtxF2L:   include/PR/gu.h:19
 *   - gVec3fZero: src/engine/graph_node.h:360 (defined graph_node.c:16)
 *
 * Unlike the lookat-only precedent's guMtxF2L (a no-op -- that test never
 * calls mtxf_rotate_xy/mtxf_to_mtx, so its return value never mattered),
 * THIS test's guMtxF2L has to actually copy data: mtxf_rotate_xy
 * (math_util.c:596-605) builds its result in a local Mat4 and calls
 * mtxf_to_mtx (math_util.c:573-591) to hand it to the caller; compiled
 * with AVOID_UB=1 (this recipe defines it, matching every other host-diff
 * target), mtxf_to_mtx calls guMtxF2L(src, dest) rather than a raw
 * pointer-cast reinterpret. Ground-truthed, not guessed: under
 * GBI_FLOATS (active here -- F3DEX_GBI_2E=1 forces it, gbi.h:90-94),
 * `Mtx` is `struct { float m[4][4]; }` (gbi.h:1193-1195), bit-identical
 * in layout to `Mat4`, and the real engine's guMtxF2L for that exact
 * configuration (lib/src/guMtxF2L.c:44-46) is a plain
 * `memcpy(m, mf, sizeof(Mtx));` -- reproduced verbatim below, not
 * reinvented.
 */
struct Surface;
f32 find_floor(UNUSED f32 xPos, UNUSED f32 yPos, UNUSED f32 zPos,
               UNUSED struct Surface **pfloor)
{
    return 0.0f;
}
void guMtxF2L(float mf[4][4], Mtx *m)
{
    memcpy(m, mf, sizeof(Mtx));
}
Vec3f gVec3fZero = {0.0f, 0.0f, 0.0f};

#define Q16_TOL_TRIG   4        /* ulps, table-identical trig */
#define Q16_TOL_NORM   64       /* ulps (~0.001), sqrt/divide chains */
#define Q16_TOL_TRANS  1        /* ulps, exact conversions */

static int32_t f_to_q(float f) { return sm64_saturn_float_to_q16(f); }

static void assert_close(int32_t got, float want_f, int32_t tol,
                         const char *what, int i, int j)
{
    int64_t want = f_to_q(want_f);
    int64_t diff = (int64_t) got - want;
    if (diff < 0) diff = -diff;
    if (diff > tol) {
        fprintf(stderr, "%s [%d][%d]: got %d want %lld (diff %lld > %d)\n",
                what, i, j, got, (long long) want, (long long) diff, tol);
        exit(1);
    }
}

/* Bound on the Q16-ulp error a dot product sum(v_i * col_i) can pick up
 * when col_i (a Q16.16 unit-range rotation-matrix column, independently
 * verified elsewhere to agree with its float original to within
 * +-per_component_tol ulps) is dotted against a Q16.16 vector whose raw
 * components are v_q[i].
 *
 * NOT a flat multiplier of per_component_tol (that was this test's
 * original, WRONG approach -- ground-truthed as wrong via a scratch
 * diagnostic, tools/saturn scratchpad diag_lookat.c, not assumed: fixture
 * 1's lookat translation row [3][2] differed by 318974 ulps against a
 * flat Q16_TOL_NORM*16=1024 bound, while every rotation-column entry
 * feeding that same dot product was independently within 23 ulps of its
 * float original. Recomputing "-(from . col)" in float using the Q16
 * columns (converted back to float) reproduced the Q16 translation-row
 * output to 6 significant digits, confirming the translation-row
 * arithmetic itself is correct and the discrepancy is pure amplification
 * of an already-in-tolerance upstream rounding difference by the
 * "from" position's magnitude (~7208 world units for that fixture) --
 * not a transcription bug.
 *
 * Derivation: each dot-product term's real-valued error is bounded by
 * |v_i| * per_component_tol/65536 (v_i real, per_component_tol a ulp
 * count on col_i). Summing 3 terms (worst case, errors aligned) and
 * rescaling the summed real-error bound back to Q16 ulps (*65536) gives
 * 3 * per_component_tol * max(|v_q_i|) / 65536, using max(|v_q_i|) as a
 * safe per-axis stand-in rather than tracking each axis separately. */
static int32_t dot_amplified_tol(const int32_t v_q[3], int32_t per_component_tol)
{
    int64_t max_abs = 0;
    for (int i = 0; i < 3; i++) {
        int64_t a = v_q[i] < 0 ? -(int64_t) v_q[i] : (int64_t) v_q[i];
        if (a > max_abs) {
            max_abs = a;
        }
    }
    int64_t tol = (3 * (int64_t) per_component_tol * max_abs) >> 16;
    return (int32_t) (tol < per_component_tol ? per_component_tol : tol);
}

static void diff_lookat(Vec3f from, Vec3f to, s16 roll)
{
    Mat4 want;
    sm64_saturn_mtx_t got;
    int32_t fq[3] = { f_to_q(from[0]), f_to_q(from[1]), f_to_q(from[2]) };
    int32_t tq[3] = { f_to_q(to[0]), f_to_q(to[1]), f_to_q(to[2]) };
    int32_t trans_tol = dot_amplified_tol(fq, Q16_TOL_NORM);

    mtxf_lookat(want, from, to, roll);
    sm64_saturn_mtxq_lookat(&got, fq, tq, roll);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            /* row 3 (translation row, -(from . col)) amplifies the
             * rotation columns' own already-asserted error by from's
             * magnitude; see dot_amplified_tol's comment. */
            assert_close(got.m[i][j], want[i][j],
                         (i == 3) ? trans_tol : Q16_TOL_NORM,
                         "lookat", i, j);
        }
    }
    assert(got.m[0][3] == 0 && got.m[1][3] == 0 && got.m[2][3] == 0);
    assert(got.m[3][3] == (1 << 16));
}

int main(void)
{
    /* Fixture 1: the exact real captured camera state from the live
     * corruption evidence (e2-sourceboot-gmatstack-corruption doc). */
    {
        Vec3f from = { -7208.26318359375f, 264.13934326171875f, 7050.0f };
        Vec3f to = { -6566.8955078125f, 124.66311645507812f, 6454.2001953125f };
        diff_lookat(from, to, 0);
    }
    /* Fixture 2: roll sweep at the same position (64 evenly spaced) */
    {
        Vec3f from = { -7208.26318359375f, 264.13934326171875f, 7050.0f };
        Vec3f to = { -6566.8955078125f, 124.66311645507812f, 6454.2001953125f };
        for (int r = 0; r < 65536; r += 1024) {
            diff_lookat(from, to, (s16) r);
        }
    }
    /* Fixture 3: BOB spawn-scale positions, varied axes */
    {
        Vec3f from = { -6558.0f, 850.0f, 6464.0f };
        Vec3f to = { -6558.0f, 0.0f, 5000.0f };
        diff_lookat(from, to, 0);
    }

    /* rotate_zxy / rotate_xyz: full-turn sweeps on each axis */
    for (int a = 0; a < 65536; a += 4096) {
        Mat4 want;
        sm64_saturn_mtx_t got;
        Vec3f t = { 123.0f, -456.0f, 789.0f };
        Vec3s r3 = { (s16) a, (s16) (a * 3), (s16) (a * 7) };
        int32_t tq[3] = { f_to_q(t[0]), f_to_q(t[1]), f_to_q(t[2]) };

        mtxf_rotate_zxy_and_translate(want, t, r3);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&got, tq, r3[0], r3[1], r3[2]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG * 4,
                             "rot_zxy", i, j);
        for (int j = 0; j < 3; j++)
            assert_close(got.m[3][j], want[3][j], Q16_TOL_TRANS,
                         "rot_zxy_t", 3, j);

        mtxf_rotate_xyz_and_translate(want, t, r3);
        sm64_saturn_mtxq_rotate_xyz_and_translate(&got, tq, r3[0], r3[1], r3[2]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG * 4,
                             "rot_xyz", i, j);
    }

    /* billboard: uses a camera matrix -- feed the real lookat output */
    {
        Mat4 cam_f;
        sm64_saturn_mtx_t cam_q, got;
        Mat4 want;
        Vec3f from = { -7208.26f, 264.14f, 7050.0f };
        Vec3f to = { -6566.90f, 124.66f, 6454.20f };
        Vec3f pos = { -6558.0f, 100.0f, 6464.0f };
        int32_t pq[3] = { f_to_q(pos[0]), f_to_q(pos[1]), f_to_q(pos[2]) };
        int32_t fq[3] = { f_to_q(from[0]), f_to_q(from[1]), f_to_q(from[2]) };
        int32_t tq[3] = { f_to_q(to[0]), f_to_q(to[1]), f_to_q(to[2]) };

        mtxf_lookat(cam_f, from, to, 0);
        sm64_saturn_mtxq_lookat(&cam_q, fq, tq, 0);
        /* dest[3][c] = mtx[0][c]*position[0] + mtx[1][c]*position[1] +
         * mtx[2][c]*position[2] + mtx[3][c] (math_util.c:358-364): two
         * independent amplified-error sources feed this cell, so their
         * bounds add. (1) position dotted against cam_q's own rotation
         * columns, each already within Q16_TOL_NORM of cam_f's (same
         * mechanism as lookat's translation row, see
         * dot_amplified_tol's comment). (2) mtx[3][c] itself is
         * cam_q's translation row, carried through unchanged --
         * exactly the quantity lookat's own diff_lookat test already
         * bounds via dot_amplified_tol(fq, ...) for this same from/to. */
        int32_t billboard_pos_tol = dot_amplified_tol(pq, Q16_TOL_NORM)
                                   + dot_amplified_tol(fq, Q16_TOL_NORM);
        for (int a = 0; a < 65536; a += 8192) {
            mtxf_billboard(want, cam_f, pos, (s16) a);
            sm64_saturn_mtxq_billboard(&got, &cam_q, pq, (s16) a);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG,
                                 "billboard_rot", i, j);
            for (int j = 0; j < 3; j++)
                assert_close(got.m[3][j], want[3][j], billboard_pos_tol,
                             "billboard_pos", 3, j);
        }
    }

    /* scale + translate: exact-ish */
    {
        Mat4 base_f, want;
        sm64_saturn_mtx_t base_q, got;
        Vec3f s = { 0.5f, 2.0f, 1.25f };
        Vec3f t = { 10.0f, -20.0f, 30.0f };
        int32_t sq[3] = { f_to_q(s[0]), f_to_q(s[1]), f_to_q(s[2]) };
        int32_t tq[3] = { f_to_q(t[0]), f_to_q(t[1]), f_to_q(t[2]) };

        mtxf_identity(base_f);
        sm64_saturn_matrix_identity(&base_q);
        mtxf_scale_vec3f(want, base_f, s);
        sm64_saturn_mtxq_scale_vec3f(&got, &base_q, sq);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG,
                             "scale", i, j);

        mtxf_translate(want, t);
        sm64_saturn_mtxq_translate(&got, tq);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRANS,
                             "translate", i, j);
    }

    /* rotate_xy: Z-axis screen-roll matrix used by the HUD/ortho camera
     * node (rendering_graph_node.c:323).
     *
     * NOT present in the task-3 test-plan text as dispatched -- added
     * here to close a real coverage gap: the plan's own opening
     * paragraph requires every constructor in the render-graph
     * inventory to be "differential-tested on host against the real
     * float original," and Step 3 implements sm64_saturn_mtxq_rotate_xy,
     * but the given harness never called mtxf_rotate_xy /
     * sm64_saturn_mtxq_rotate_xy anywhere. See the guMtxF2L stub comment
     * above for why the real target function can be called directly
     * (through a real, ground-truthed guMtxF2L) rather than hand-copying
     * math_util.c's logic into this test. */
    {
        Mtx want_wire;
        sm64_saturn_mtx_t got;

        for (int a = 0; a < 65536; a += 4096) {
            mtxf_rotate_xy(&want_wire, (s16) a);
            sm64_saturn_mtxq_rotate_xy(&got, (s16) a);
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    assert_close(got.m[i][j], want_wire.m[i][j], Q16_TOL_TRIG,
                                 "rotate_xy", i, j);
        }
    }

    printf("mtxq ctor differential: all fixtures within tolerance\n");
    return 0;
}
