#include "saturn_actor_rigid_pose.h"

#include "saturn_matrix_ctors.h"
#include "port/saturn/platform/saturn_cart_code.h"

/* RESIDENCY, MEASURED RATHER THAN ASSUMED.
 *
 * This lane does not fit in HWRAM at today's margin.  The first link with it
 * in ordinary `.text` was refused outright:
 *
 *   sh-elf-ld: HWRAM margin below libyaul's TLSF control-block floor: the
 *   heap libyaul builds at ___end would overrun the top of HWRAM
 *
 * The cost is duplication, not bloat.  `sm64_saturn_mtxq_rotate_xyz_and_
 * translate` (six trig lookups and eleven Q16.16 multiplies) and
 * `sm64_saturn_matrix_mul` (a 4x4 multiply with per-term overflow checks) are
 * `static inline` in headers, so this translation unit gets its own copy of
 * both -- the skinned lane in saturn_actor_pose.c already has one, and HWRAM
 * had roughly a kilobyte of margin, not two.
 *
 * So the lane is linked cold, into the cart text section, and that is stated
 * plainly rather than hidden: today it has no per-frame caller (nothing
 * spawns), and its only caller is the boot-time exercise in
 * saturn_actor_bridge.c, for which cart residency is exactly right.  The
 * moment a real enemy is wired to it per frame, this annotation has to come
 * off and the HWRAM has to be found -- the deletion of the pool's evidence
 * scaffolding (r2-pose-slot-pooling.md section 8.7, ~1.1 KB) is the funding
 * already identified.  See the residency section of
 * r2-rigid-part-evaluator.md for the measured size. */
#define SM64_SATURN_RIGID_LANE SM64_SATURN_CART_COLD

/* Mutation hooks.  Each is compiled in only by the mutation gate and must
 * make `verify-actor-rigid-pose` fail; see Makefile.saturn.mk. */
#ifndef SM64_SATURN_RIGID_POSE_MUTATE_TRANSPOSE
#define SM64_SATURN_RIGID_POSE_MUTATE_TRANSPOSE 0
#endif
#ifndef SM64_SATURN_RIGID_POSE_MUTATE_SKIP_PARENT
#define SM64_SATURN_RIGID_POSE_MUTATE_SKIP_PARENT 0
#endif
#ifndef SM64_SATURN_RIGID_POSE_MUTATE_ALIAS
#define SM64_SATURN_RIGID_POSE_MUTATE_ALIAS 0
#endif

SM64_SATURN_RIGID_LANE
int16_t sm64_saturn_rigid_q15_from_q16(int32_t value)
{
    /* Q16.16 carries 16 fractional bits, Q1.15 carries 15, so the conversion
     * is a halving.  Round half up, then saturate: a rotation entry of
     * exactly +1.0 (65536) would need 32768, which Q1.15 cannot hold, so it
     * clamps to 32767 -- a 3.05e-5 error on an axis-aligned part, which is
     * below one unit at any actor size this bake produces. */
    const int32_t halved = (value + 1) >> 1;
    if (halved > INT16_MAX) return INT16_MAX;
    if (halved < INT16_MIN) return INT16_MIN;
    return (int16_t)halved;
}

SM64_SATURN_RIGID_LANE
static int16_t clamp_s16(int32_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

SM64_SATURN_RIGID_LANE
static void publish_part(const sm64_saturn_mtx_t *composed,
                         sm64_saturn_rigid_part_t *part)
{
    uint16_t row, column;
    for (row = 0U; row < 3U; row++) {
        for (column = 0U; column < 3U; column++) {
#if SM64_SATURN_RIGID_POSE_MUTATE_TRANSPOSE
            part->rotation[column * 3U + row] =
                sm64_saturn_rigid_q15_from_q16(composed->m[row][column]);
#else
            part->rotation[row * 3U + column] =
                sm64_saturn_rigid_q15_from_q16(composed->m[row][column]);
#endif
        }
    }
    /* Row 3 of the Q16.16 matrix carries the translation in raw actor units,
     * not Q16.16 -- the same split `matrix_apply` relies on in the skinned
     * lane, and the reason `sm64_saturn_matrix_mul` composes translations
     * correctly with m[3][3] == 1<<16. */
    for (column = 0U; column < 3U; column++)
        part->translation[column] = clamp_s16(composed->m[3][column]);
}

SM64_SATURN_RIGID_LANE
bool sm64_saturn_actor_rigid_pose_compose(
    const sm64_saturn_actor_joint_t *joints, uint16_t part_count,
    const int16_t (*angles)[3], const int16_t root_translation[3],
    sm64_saturn_mtx_t *workspace,
    sm64_saturn_rigid_pose_t *out, uint32_t out_bytes)
{
    uint16_t part;

#if SM64_SATURN_RIGID_POSE_MUTATE_ALIAS
    /* Two actors' evaluations land on the same bytes. */
    static sm64_saturn_rigid_pose_t *first_out;
    if (first_out == NULL) first_out = out;
    out = first_out;
#endif

    if (out == NULL || out_bytes < SM64_SATURN_RIGID_POSE_HEADER_BYTES)
        return false;
    out->flags = 0U;
    out->animation_id = 0U;
    out->frame = 0U;
    out->frame_count = 0U;
    out->part_count = 0U;
    out->root_translation[0] = 0;
    out->root_translation[1] = 0;
    out->root_translation[2] = 0;

    if (part_count > SM64_SATURN_RIGID_POSE_MAX_PARTS ||
        out_bytes < SM64_SATURN_RIGID_POSE_BYTES(part_count))
        return false;
    if (part_count != 0U && (joints == NULL || angles == NULL ||
                             workspace == NULL))
        return false;

    if (root_translation != NULL) {
        out->root_translation[0] = root_translation[0];
        out->root_translation[1] = root_translation[1];
        out->root_translation[2] = root_translation[2];
    }

    for (part = 0U; part < part_count; part++) {
        const int16_t parent = joints[part].parent_ordinal;
        int32_t translation[3];
        sm64_saturn_mtx_t local;
        uint16_t axis;

        /* The bake emits parents before children.  Anything else is a
         * malformed hierarchy: reject the whole actor rather than compose
         * against uninitialised workspace. */
        if (parent < -1 || (parent >= 0 && (uint16_t)parent >= part))
            return false;

        for (axis = 0U; axis < 3U; axis++) {
            translation[axis] = (int32_t)joints[part].translation[axis];
            if (part == 0U)
                translation[axis] += (int32_t)out->root_translation[axis];
        }
        sm64_saturn_mtxq_rotate_xyz_and_translate(
            &local, translation, angles[part][0], angles[part][1],
            angles[part][2]);

#if SM64_SATURN_RIGID_POSE_MUTATE_SKIP_PARENT
        workspace[part] = local;
#else
        if (parent < 0) {
            workspace[part] = local;
        } else if (sm64_saturn_matrix_mul(&local, &workspace[parent],
                                          &workspace[part])) {
            return false;
        }
#endif
        publish_part(&workspace[part], &out->part[part]);
    }

    out->part_count = part_count;
    out->flags = SM64_SATURN_RIGID_POSE_VALID;
    return true;
}

SM64_SATURN_RIGID_LANE
bool sm64_saturn_actor_rigid_pose_evaluate(
    const sm64_saturn_actor_bank_view_t *bank,
    int16_t animation_id, int16_t animation_frame,
    const sm64_saturn_actor_joint_t *staged_joints,
    sm64_saturn_mtx_t *workspace,
    sm64_saturn_rigid_pose_t *out, uint32_t out_bytes)
{
    sm64_saturn_actor_animation_record_t record;
    sm64_saturn_actor_joint_t joints[SM64_SATURN_RIGID_POSE_MAX_PARTS];
    int16_t angles[SM64_SATURN_RIGID_POSE_MAX_PARTS][3];
    int16_t root_translation[3] = { 0, 0, 0 };
    int32_t frame;
    uint16_t part, axis, part_count;

    if (out == NULL || out_bytes < SM64_SATURN_RIGID_POSE_HEADER_BYTES)
        return false;
    out->flags = 0U;
    if (bank == NULL || workspace == NULL || animation_id < 0 ||
        (uint16_t)animation_id >= bank->bank.animation_count ||
        bank->bank.joint_count > SM64_SATURN_RIGID_POSE_MAX_PARTS ||
        !sm64_saturn_actor_bank_animation(bank, (uint16_t)animation_id,
                                          &record) ||
        record.frame_count == 0U)
        return false;

    part_count = bank->bank.joint_count;
    frame = animation_frame < 0 ? 0 : (int32_t)animation_frame;
    if ((uint32_t)frame >= record.frame_count)
        frame = (int32_t)record.frame_count - 1;

    /* Channels 0..2 are the root translation; 3 + joint*3 + axis are the
     * per-joint Euler samples.  Identical to the skinned lane, because it is
     * the same bank format and the same animation stream. */
    for (axis = 0U; axis < 3U; axis++) {
        int16_t sample = 0;
        if (!sm64_saturn_actor_bank_sample_channel(
                bank, (uint16_t)animation_id, (uint16_t)frame, axis, &sample))
            return false;
        root_translation[axis] = sample;
    }
    for (part = 0U; part < part_count; part++) {
        if (staged_joints != NULL)
            joints[part] = staged_joints[part];
        else if (!sm64_saturn_actor_bank_joint(bank, part, &joints[part]))
            return false;
        for (axis = 0U; axis < 3U; axis++) {
            int16_t sample = 0;
            const uint16_t channel = (uint16_t)(3U + part * 3U + axis);
            if (!sm64_saturn_actor_bank_sample_channel(
                    bank, (uint16_t)animation_id, (uint16_t)frame, channel,
                    &sample))
                return false;
            angles[part][axis] = sample;
        }
    }

    /* ISO C before C2x does not implicitly add const through a
     * pointer-to-array. */
    if (!sm64_saturn_actor_rigid_pose_compose(
            joints, part_count, (const int16_t (*)[3])angles, root_translation,
            workspace, out, out_bytes))
        return false;
    out->animation_id = (uint16_t)animation_id;
    out->frame = (uint16_t)frame;
    out->frame_count = record.frame_count;
    return true;
}
