/* Host fixture for the rigid-part pose evaluator.
 *
 * The evaluator's job is to turn a joint hierarchy plus one frame of s16
 * Euler samples into an array of *composed, model-space* part transforms in
 * the published 24 B layout.  This fixture checks that against two oracles:
 *
 *   - the documented rotation formula, written out longhand here rather than
 *     called through the ctor, for the single-part cases where the answer is
 *     a known matrix; and
 *   - an independent double-precision composition, for the parent/child
 *     cases where hand-deriving the answer would be its own bug farm.
 *
 * Everything is stated through the *consumer contract* wherever it can be:
 * probe vertices are pushed through the published Q1.15 rotation and int16
 * translation exactly as a renderer would, because that is the property that
 * matters and it is the one a transposed matrix breaks.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_actor_rigid_pose.h"
#include "saturn_matrix_ctors.h"

/* ISO C before C2x does not implicitly add const through a pointer-to-array,
 * so the fixture's mutable angle tables need an explicit cast. */
#define ANG(a) ((const int16_t (*)[3])(a))

static int g_failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static void check_eq_i32(int32_t got, int32_t want, const char *what)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %ld, want %ld)\n", what, (long)got,
                (long)want);
        g_failures++;
    }
}

/* ---- storage ---------------------------------------------------------- */

#define POSE_BYTES SM64_SATURN_RIGID_POSE_BYTES(SM64_SATURN_RIGID_POSE_MAX_PARTS)

typedef union pose_block {
    sm64_saturn_rigid_pose_t pose;
    uint8_t bytes[POSE_BYTES];
} pose_block_t;

/* ---- the consumer contract, written as a renderer would ---------------- */

static void apply_part(const sm64_saturn_rigid_part_t *part,
                       const int16_t local[3], int32_t out[3])
{
    int column;
    for (column = 0; column < 3; column++) {
        int32_t sum = 0;
        int row;
        for (row = 0; row < 3; row++)
            sum += (int32_t)local[row] * (int32_t)part->rotation[row * 3 + column];
        out[column] = (sum >> 15) + (int32_t)part->translation[column];
    }
}

/* ---- the independent double oracle ------------------------------------ */

typedef struct ref_mtx { double m[4][4]; } ref_mtx_t;

static double angle_radians(int16_t angle)
{
    return (double)angle * (2.0 * 3.14159265358979323846) / 65536.0;
}

/* Same shape as sm64_saturn_mtxq_rotate_xyz_and_translate, in doubles. */
static void ref_rotate_xyz_and_translate(ref_mtx_t *dest, const double b[3],
                                         int16_t rx, int16_t ry, int16_t rz)
{
    const double sx = sin(angle_radians(rx)), cx = cos(angle_radians(rx));
    const double sy = sin(angle_radians(ry)), cy = cos(angle_radians(ry));
    const double sz = sin(angle_radians(rz)), cz = cos(angle_radians(rz));
    memset(dest, 0, sizeof(*dest));
    dest->m[0][0] = cy * cz;
    dest->m[0][1] = cy * sz;
    dest->m[0][2] = -sy;
    dest->m[1][0] = sx * sy * cz - cx * sz;
    dest->m[1][1] = sx * sy * sz + cx * cz;
    dest->m[1][2] = sx * cy;
    dest->m[2][0] = cx * sy * cz + sx * sz;
    dest->m[2][1] = cx * sy * sz - sx * cz;
    dest->m[2][2] = cx * cy;
    dest->m[3][0] = b[0];
    dest->m[3][1] = b[1];
    dest->m[3][2] = b[2];
    dest->m[3][3] = 1.0;
}

/* out = a * b, the same order sm64_saturn_matrix_mul uses. */
static void ref_mul(const ref_mtx_t *a, const ref_mtx_t *b, ref_mtx_t *out)
{
    ref_mtx_t tmp;
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            double sum = 0.0;
            for (k = 0; k < 4; k++) sum += a->m[i][k] * b->m[k][j];
            tmp.m[i][j] = sum;
        }
    }
    *out = tmp;
}

static void ref_apply(const ref_mtx_t *m, const int16_t local[3],
                      double out[3])
{
    int column;
    for (column = 0; column < 3; column++) {
        out[column] = (double)local[0] * m->m[0][column] +
                      (double)local[1] * m->m[1][column] +
                      (double)local[2] * m->m[2][column] + m->m[3][column];
    }
}

/* Tolerance against the double oracle, on a deliberately oversized 1,000-unit
 * probe vertex at hierarchy depth 4.  test_q15_penalty below measures how
 * much of this the published Q1.15 layout is actually responsible for versus
 * the Q16.16 composition it quantises; the answer is what justifies 24 B per
 * part instead of 48.  Every mutation this fixture gates on misses by
 * hundreds of units, so the exact ceiling is not load-bearing. */
#define PROBE_TOLERANCE 4.0

static const int16_t g_probe[4][3] = {
    { 1000, 0, 0 }, { 0, 1000, 0 }, { 0, 0, 1000 }, { 300, -700, 450 },
};

static void check_part_against_reference(const sm64_saturn_rigid_part_t *part,
                                         const ref_mtx_t *reference,
                                         const char *what)
{
    int probe;
    for (probe = 0; probe < 4; probe++) {
        int32_t got[3];
        double want[3];
        int axis;
        apply_part(part, g_probe[probe], got);
        ref_apply(reference, g_probe[probe], want);
        for (axis = 0; axis < 3; axis++) {
            const double error = fabs((double)got[axis] - want[axis]);
            if (!(error <= PROBE_TOLERANCE)) {
                fprintf(stderr,
                        "FAIL: %s probe %d axis %d off by %.3f "
                        "(got %ld, want %.3f)\n",
                        what, probe, axis, error, (long)got[axis], want[axis]);
                g_failures++;
                return;
            }
        }
    }
}

/* Compose the same chain in Q16.16 and apply a probe vertex the way the
 * skinned lane's matrix_apply does, so the published Q1.15 error can be
 * separated from the error the fixed-point composition already had. */
static void q16_apply(const sm64_saturn_mtx_t *m, const int16_t local[3],
                      int32_t out[3])
{
    int column;
    for (column = 0; column < 3; column++) {
        const int64_t sum = (int64_t)local[0] * m->m[0][column] +
                            (int64_t)local[1] * m->m[1][column] +
                            (int64_t)local[2] * m->m[2][column];
        out[column] = (int32_t)((sum >> 16) + m->m[3][column]);
    }
}

/* ---- fixtures --------------------------------------------------------- */

static void test_q15_conversion(void)
{
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(0), 0, "q15(0)");
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(32768), 16384, "q15(+0.5)");
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(-32768), -16384, "q15(-0.5)");
    /* +1.0 has no Q1.15 representation and must saturate, not wrap. */
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(65536), 32767, "q15(+1.0)");
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(-65536), -32768, "q15(-1.0)");
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(1 << 30), 32767,
                 "q15 saturates high");
    check_eq_i32(sm64_saturn_rigid_q15_from_q16(-(1 << 30)), -32768,
                 "q15 saturates low");
}

/* The single-part known-matrix case: the rotation formula written out here
 * rather than taken from the ctor, so agreement is evidence and not a
 * tautology. */
static void expected_rotation_q15(int16_t rx, int16_t ry, int16_t rz,
                                  int16_t out[9])
{
    const int32_t sx = sm64_saturn_sins_q16(rx), cx = sm64_saturn_coss_q16(rx);
    const int32_t sy = sm64_saturn_sins_q16(ry), cy = sm64_saturn_coss_q16(ry);
    const int32_t sz = sm64_saturn_sins_q16(rz), cz = sm64_saturn_coss_q16(rz);
    const int32_t sxsy = sm64_saturn_q16_mul(sx, sy);
    const int32_t cxsy = sm64_saturn_q16_mul(cx, sy);
    const int32_t q16[9] = {
        sm64_saturn_q16_mul(cy, cz),
        sm64_saturn_q16_mul(cy, sz),
        -sy,
        sm64_saturn_q16_mul(sxsy, cz) - sm64_saturn_q16_mul(cx, sz),
        sm64_saturn_q16_mul(sxsy, sz) + sm64_saturn_q16_mul(cx, cz),
        sm64_saturn_q16_mul(sx, cy),
        sm64_saturn_q16_mul(cxsy, cz) + sm64_saturn_q16_mul(sx, sz),
        sm64_saturn_q16_mul(cxsy, sz) - sm64_saturn_q16_mul(sx, cz),
        sm64_saturn_q16_mul(cx, cy),
    };
    int index;
    for (index = 0; index < 9; index++)
        out[index] = sm64_saturn_rigid_q15_from_q16(q16[index]);
}

static void test_single_part_known_matrix(void)
{
    static const struct { int16_t rx, ry, rz; const char *label; } cases[] = {
        { 0, 0, 0, "identity" },
        { 0, 0, 0x4000, "z+90" },
        { 0x2000, 0, 0, "x+45" },
        { 0x1234, (int16_t)0xC000, 0x0555, "arbitrary" },
    };
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        sm64_saturn_actor_joint_t joint = { -1, { 12, -34, 56 }, 0, 0 };
        int16_t angles[1][3];
        int16_t want[9];
        sm64_saturn_mtx_t workspace[1];
        pose_block_t block;
        int entry;
        char what[96];

        angles[0][0] = cases[index].rx;
        angles[0][1] = cases[index].ry;
        angles[0][2] = cases[index].rz;
        expected_rotation_q15(cases[index].rx, cases[index].ry,
                              cases[index].rz, want);
        memset(&block, 0, sizeof(block));
        check(sm64_saturn_actor_rigid_pose_compose(&joint, 1U, ANG(angles), NULL,
                                                   workspace, &block.pose,
                                                   POSE_BYTES),
              "single part composes");
        check_eq_i32(block.pose.part_count, 1, "single part count");
        check_eq_i32(block.pose.flags, SM64_SATURN_RIGID_POSE_VALID,
                     "single part is published valid");
        for (entry = 0; entry < 9; entry++) {
            snprintf(what, sizeof(what), "%s rotation[%d]",
                     cases[index].label, entry);
            check_eq_i32(block.pose.part[0].rotation[entry], want[entry], what);
        }
        check_eq_i32(block.pose.part[0].translation[0], 12,
                     "root translation x passes through");
        check_eq_i32(block.pose.part[0].translation[1], -34,
                     "root translation y passes through");
        check_eq_i32(block.pose.part[0].translation[2], 56,
                     "root translation z passes through");
    }
}

/* Identity must be exactly identity, because a stiff actor that is drawn
 * at a slightly wrong bind pose is the failure this whole lane exists to
 * avoid, and it is the one case where the answer is not debatable. */
static void test_identity_is_identity(void)
{
    sm64_saturn_actor_joint_t joint = { -1, { 0, 0, 0 }, 0, 0 };
    int16_t angles[1][3] = { { 0, 0, 0 } };
    sm64_saturn_mtx_t workspace[1];
    pose_block_t block;
    static const int16_t want[9] = { 32767, 0, 0, 0, 32767, 0, 0, 0, 32767 };
    int entry;
    memset(&block, 0, sizeof(block));
    check(sm64_saturn_actor_rigid_pose_compose(&joint, 1U, ANG(angles), NULL,
                                               workspace, &block.pose,
                                               POSE_BYTES),
          "identity composes");
    for (entry = 0; entry < 9; entry++) {
        char what[64];
        snprintf(what, sizeof(what), "identity rotation[%d]", entry);
        check_eq_i32(block.pose.part[0].rotation[entry], want[entry], what);
    }
}

/* The hierarchy the whole lane is for: a five-part chain with rotation and
 * translation at every level, checked against the double oracle. */
#define CHAIN_PARTS 5U

static void build_chain(sm64_saturn_actor_joint_t joints[CHAIN_PARTS],
                        int16_t angles[CHAIN_PARTS][3], int seed)
{
    static const int16_t parents[CHAIN_PARTS] = { -1, 0, 1, 1, 2 };
    uint16_t part;
    for (part = 0U; part < CHAIN_PARTS; part++) {
        joints[part].parent_ordinal = parents[part];
        joints[part].node_ordinal = (int16_t)part;
        joints[part].branch_ordinal = 0U;
        joints[part].translation[0] = (int16_t)(40 + 11 * (int)part);
        joints[part].translation[1] = (int16_t)(-25 + 7 * (int)part);
        joints[part].translation[2] = (int16_t)(13 * (int)part);
        angles[part][0] = (int16_t)(0x0800 * (int)(part + 1) + 0x0100 * seed);
        angles[part][1] = (int16_t)(0x1500 - 0x0300 * (int)part + 0x0200 * seed);
        angles[part][2] = (int16_t)(0x0400 * (int)part - 0x0700 * seed);
    }
}

static void reference_chain(const sm64_saturn_actor_joint_t *joints,
                            const int16_t (*angles)[3],
                            const int16_t root_translation[3],
                            ref_mtx_t reference[CHAIN_PARTS])
{
    uint16_t part;
    for (part = 0U; part < CHAIN_PARTS; part++) {
        double translation[3];
        ref_mtx_t local;
        int axis;
        for (axis = 0; axis < 3; axis++) {
            translation[axis] = (double)joints[part].translation[axis];
            if (part == 0U && root_translation != NULL)
                translation[axis] += (double)root_translation[axis];
        }
        ref_rotate_xyz_and_translate(&local, translation, angles[part][0],
                                     angles[part][1], angles[part][2]);
        if (joints[part].parent_ordinal < 0)
            reference[part] = local;
        else
            ref_mul(&local, &reference[joints[part].parent_ordinal],
                    &reference[part]);
    }
}

static void test_parent_child_composition(void)
{
    sm64_saturn_actor_joint_t joints[CHAIN_PARTS];
    int16_t angles[CHAIN_PARTS][3];
    int16_t root_translation[3] = { -120, 300, 45 };
    ref_mtx_t reference[CHAIN_PARTS];
    sm64_saturn_mtx_t workspace[CHAIN_PARTS];
    pose_block_t block;
    uint16_t part;

    build_chain(joints, angles, 1);
    reference_chain(joints, ANG(angles), root_translation, reference);
    memset(&block, 0, sizeof(block));
    check(sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles),
                                               root_translation, workspace,
                                               &block.pose, POSE_BYTES),
          "chain composes");
    check_eq_i32(block.pose.part_count, CHAIN_PARTS, "chain part count");
    check_eq_i32(block.pose.root_translation[0], -120, "chain root x");
    check_eq_i32(block.pose.root_translation[1], 300, "chain root y");
    check_eq_i32(block.pose.root_translation[2], 45, "chain root z");
    for (part = 0U; part < CHAIN_PARTS; part++) {
        char what[64];
        snprintf(what, sizeof(what), "chain part %u", (unsigned)part);
        check_part_against_reference(&block.pose.part[part], &reference[part],
                                     what);
    }

    /* A child must actually move when its parent rotates.  Without this the
     * fixture would pass on a producer that published every part at its own
     * local transform. */
    {
        int16_t alternative[CHAIN_PARTS][3];
        pose_block_t moved;
        uint16_t axis;
        int differed = 0;
        memcpy(alternative, ANG(angles), sizeof(alternative));
        alternative[0][1] = (int16_t)(alternative[0][1] + 0x2000);
        memset(&moved, 0, sizeof(moved));
        check(sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS,
                                                   ANG(alternative),
                                                   root_translation, workspace,
                                                   &moved.pose, POSE_BYTES),
              "rotated-root chain composes");
        for (axis = 0U; axis < 3U; axis++) {
            if (moved.pose.part[CHAIN_PARTS - 1U].translation[axis] !=
                block.pose.part[CHAIN_PARTS - 1U].translation[axis])
                differed = 1;
        }
        check(differed, "rotating the root moves the deepest child");
    }
}

/* Two actors evaluated back to back must land in their own bytes. */
static void test_two_actors_do_not_alias(void)
{
    sm64_saturn_actor_joint_t joints[CHAIN_PARTS];
    int16_t angles_a[CHAIN_PARTS][3];
    int16_t angles_b[CHAIN_PARTS][3];
    ref_mtx_t reference_a[CHAIN_PARTS];
    sm64_saturn_mtx_t workspace[CHAIN_PARTS];
    pose_block_t block_a, block_b;
    uint16_t part;
    int differed = 0;

    build_chain(joints, angles_a, 1);
    build_chain(joints, angles_b, 4);
    reference_chain(joints, ANG(angles_a), NULL, reference_a);

    memset(&block_a, 0, sizeof(block_a));
    memset(&block_b, 0, sizeof(block_b));
    check(sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles_a),
                                               NULL, workspace, &block_a.pose,
                                               POSE_BYTES),
          "actor A composes");
    check(sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles_b),
                                               NULL, workspace, &block_b.pose,
                                               POSE_BYTES),
          "actor B composes");
    for (part = 0U; part < CHAIN_PARTS; part++) {
        if (memcmp(&block_a.pose.part[part], &block_b.pose.part[part],
                   sizeof(sm64_saturn_rigid_part_t)) != 0)
            differed = 1;
    }
    check(differed, "two different actors do not produce identical poses");
    /* The load-bearing assertion: A still holds A after B ran. */
    for (part = 0U; part < CHAIN_PARTS; part++) {
        char what[64];
        snprintf(what, sizeof(what), "actor A part %u survives actor B",
                 (unsigned)part);
        check_part_against_reference(&block_a.pose.part[part],
                                     &reference_a[part], what);
    }
}

/* The layout decision, measured rather than asserted.  If the 24 B published
 * form were the dominant error source, 48 B would be worth its bytes; it is
 * not, so it is not. */
static void test_q15_penalty(void)
{
    sm64_saturn_actor_joint_t joints[CHAIN_PARTS];
    int16_t angles[CHAIN_PARTS][3];
    ref_mtx_t reference[CHAIN_PARTS];
    sm64_saturn_mtx_t workspace[CHAIN_PARTS];
    pose_block_t block;
    double worst_published = 0.0, worst_q16 = 0.0;
    uint16_t part;
    int probe, axis;

    build_chain(joints, angles, 3);
    reference_chain(joints, ANG(angles), NULL, reference);
    memset(&block, 0, sizeof(block));
    check(sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS,
                                               ANG(angles), NULL, workspace,
                                               &block.pose, POSE_BYTES),
          "penalty chain composes");
    /* `workspace` now holds the Q16.16 transforms the publish quantised. */
    for (part = 0U; part < CHAIN_PARTS; part++) {
        for (probe = 0; probe < 4; probe++) {
            int32_t published[3], q16[3];
            double want[3];
            apply_part(&block.pose.part[part], g_probe[probe], published);
            q16_apply(&workspace[part], g_probe[probe], q16);
            ref_apply(&reference[part], g_probe[probe], want);
            for (axis = 0; axis < 3; axis++) {
                const double a = fabs((double)published[axis] - want[axis]);
                const double b = fabs((double)q16[axis] - want[axis]);
                if (a > worst_published) worst_published = a;
                if (b > worst_q16) worst_q16 = b;
            }
        }
    }
    printf("  layout penalty on a 1000-unit probe at depth %u: "
           "published Q1.15 %.3f units, Q16.16 composition %.3f units\n",
           (unsigned)CHAIN_PARTS, worst_published, worst_q16);
    /* Quantising to 24 B must not cost more than the fixed-point composition
     * it quantises already costs, plus one Q1.15 step on the probe. */
    check(worst_published <= worst_q16 + 1.0,
          "the 24 B published layout is not the dominant error source");
}

static void test_degenerate(void)
{
    sm64_saturn_mtx_t workspace[CHAIN_PARTS];
    pose_block_t block;

    /* Zero parts: a valid, empty, publishable pose.  A consumer draws
     * nothing rather than reading uninitialised bytes. */
    memset(&block, 0xCC, sizeof(block));
    check(sm64_saturn_actor_rigid_pose_compose(NULL, 0U, NULL, NULL, NULL,
                                               &block.pose, POSE_BYTES),
          "zero parts composes");
    check_eq_i32(block.pose.part_count, 0, "zero parts publishes zero parts");
    check_eq_i32(block.pose.flags, SM64_SATURN_RIGID_POSE_VALID,
                 "zero parts is valid");
    check_eq_i32(block.pose.root_translation[0], 0, "zero parts zeroes root");

    /* One part with no parent is the smallest real hierarchy. */
    {
        sm64_saturn_actor_joint_t joint = { -1, { 5, 6, 7 }, 0, 0 };
        int16_t angles[1][3] = { { 0x0111, 0x0222, 0x0333 } };
        memset(&block, 0, sizeof(block));
        check(sm64_saturn_actor_rigid_pose_compose(
                  &joint, 1U, ANG(angles), NULL, workspace, &block.pose,
                  SM64_SATURN_RIGID_POSE_BYTES(1)),
              "one part composes into an exactly-sized block");
        check_eq_i32(block.pose.part_count, 1, "one part count");
    }
}

static void test_fails_closed(void)
{
    sm64_saturn_actor_joint_t joints[CHAIN_PARTS];
    int16_t angles[CHAIN_PARTS][3];
    sm64_saturn_mtx_t workspace[CHAIN_PARTS];
    pose_block_t block;

    build_chain(joints, angles, 2);

    /* One byte short of the parts it was asked to publish. */
    memset(&block, 0, sizeof(block));
    check(!sm64_saturn_actor_rigid_pose_compose(
              joints, CHAIN_PARTS, ANG(angles), NULL, workspace, &block.pose,
              SM64_SATURN_RIGID_POSE_BYTES(CHAIN_PARTS) - 1U),
          "an undersized block is refused");
    check_eq_i32(block.pose.flags, 0, "a refused pose is not marked valid");
    check_eq_i32(block.pose.part_count, 0, "a refused pose publishes no parts");

    /* A forward reference is a malformed hierarchy. */
    joints[2].parent_ordinal = 3;
    memset(&block, 0, sizeof(block));
    check(!sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles),
                                                NULL, workspace, &block.pose,
                                                POSE_BYTES),
          "a forward parent reference is refused");
    check_eq_i32(block.pose.flags, 0, "a malformed hierarchy is not valid");
    joints[2].parent_ordinal = 1;

    /* More parts than a single pose may hold. */
    memset(&block, 0, sizeof(block));
    check(!sm64_saturn_actor_rigid_pose_compose(
              joints, SM64_SATURN_RIGID_POSE_MAX_PARTS + 1U, ANG(angles), NULL,
              workspace, &block.pose, 0xFFFFU),
          "more parts than the ceiling is refused");

    check(!sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles),
                                                NULL, workspace, NULL,
                                                POSE_BYTES),
          "a NULL output block is refused");
    check(!sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, NULL,
                                                NULL, workspace, &block.pose,
                                                POSE_BYTES),
          "NULL angles are refused");
    check(!sm64_saturn_actor_rigid_pose_compose(joints, CHAIN_PARTS, ANG(angles),
                                                NULL, NULL, &block.pose,
                                                POSE_BYTES),
          "a NULL workspace is refused");
}

/* The size claim the pool class plan is derived from. */
static void test_published_size(void)
{
    check_eq_i32((int32_t)sizeof(sm64_saturn_rigid_part_t), 24,
                 "a published part is 24 B");
    check_eq_i32((int32_t)SM64_SATURN_RIGID_POSE_BYTES(20), 496,
                 "a 20-part pose is 496 B");
    check_eq_i32((int32_t)SM64_SATURN_RIGID_POSE_BYTES(10), 256,
                 "a 10-part pose is exactly the small class");
    check_eq_i32((int32_t)SM64_SATURN_RIGID_POSE_BYTES(
                     SM64_SATURN_RIGID_POSE_MAX_PARTS), 592,
                 "the largest pose fits the 640 B class");
}

/* Optional real-data arm: run the shipped Mario bank -- the only actor bank
 * that exists today -- through the *rigid* lane.  It is not how Mario is
 * drawn (he keeps the skinned lane, untouched), but it is a real 20-joint
 * hierarchy with a real animation stream, which is exactly what the bank
 * wrapper has to survive. */
static void test_real_bank(const char *path)
{
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_mtx_t workspace[SM64_SATURN_RIGID_POSE_MAX_PARTS];
    pose_block_t block;
    uint8_t *bytes;
    long size;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "FAIL: cannot open bank %s\n", path);
        g_failures++;
        return;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || fread(bytes, 1U, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "FAIL: cannot read bank %s\n", path);
        g_failures++;
        fclose(file);
        free(bytes);
        return;
    }
    fclose(file);
    if (!sm64_saturn_actor_bank_validate(bytes, (size_t)size, &view)) {
        fprintf(stderr, "FAIL: bank %s did not validate\n", path);
        g_failures++;
        free(bytes);
        return;
    }
    memset(&block, 0, sizeof(block));
    check(sm64_saturn_actor_rigid_pose_evaluate(&view, 0, 0, NULL, workspace,
                                                &block.pose, POSE_BYTES),
          "the shipped bank evaluates through the rigid lane");
    check_eq_i32(block.pose.part_count, view.bank.joint_count,
                 "the rigid pose publishes one part per bank joint");
    check_eq_i32(block.pose.flags, SM64_SATURN_RIGID_POSE_VALID,
                 "the bank-driven pose is published valid");
    check(SM64_SATURN_RIGID_POSE_BYTES(view.bank.joint_count) <= 640U,
          "the shipped bank's hierarchy fits the large cold class");

    /* Different frames of a real animation must not publish identical
     * transforms, or the actor would be stiff rather than animated. */
    {
        pose_block_t later;
        memset(&later, 0, sizeof(later));
        check(sm64_saturn_actor_rigid_pose_evaluate(&view, 0, 3, NULL,
                                                    workspace, &later.pose,
                                                    POSE_BYTES),
              "a later frame evaluates");
        check(memcmp(block.pose.part, later.pose.part,
                     sizeof(sm64_saturn_rigid_part_t) *
                         block.pose.part_count) != 0,
              "two frames of a real animation differ");
    }

    /* Out-of-range ids and a block that cannot hold the hierarchy fail
     * closed rather than publishing rubbish. */
    memset(&block, 0, sizeof(block));
    check(!sm64_saturn_actor_rigid_pose_evaluate(
              &view, (int16_t)view.bank.animation_count, 0, NULL, workspace,
              &block.pose, POSE_BYTES),
          "an out-of-range animation id is refused");
    check_eq_i32(block.pose.flags, 0, "a refused bank pose is not valid");
    check(!sm64_saturn_actor_rigid_pose_evaluate(
              &view, 0, 0, NULL, workspace, &block.pose,
              SM64_SATURN_RIGID_POSE_HEADER_BYTES),
          "a header-only block is refused for a jointed bank");
    free(bytes);
}

int main(int argc, char **argv)
{
    test_q15_conversion();
    test_published_size();
    test_identity_is_identity();
    test_single_part_known_matrix();
    test_parent_child_composition();
    test_two_actors_do_not_alias();
    test_q15_penalty();
    test_degenerate();
    test_fails_closed();
    if (argc > 1) test_real_bank(argv[1]);

    if (g_failures != 0) {
        fprintf(stderr, "rigid-pose fixture: %d failing assertions\n",
                g_failures);
        return 1;
    }
    printf("rigid-pose fixture: all checks passed\n");
    return 0;
}
