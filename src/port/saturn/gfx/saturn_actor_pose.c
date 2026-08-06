#include "saturn_actor_pose.h"

#include <limits.h>
#include <string.h>

#include "saturn_matrix_ctors.h"

static int32_t clamp_s16_frame(int16_t frame)
{
    return frame < 0 ? 0 : (int32_t)frame;
}

static void matrix_apply(const sm64_saturn_mtx_t *matrix,
                         const int16_t local[3], int16_t out[3])
{
    int64_t x, y, z;
    x = ((int64_t)local[0] * matrix->m[0][0] +
         (int64_t)local[1] * matrix->m[1][0] +
         (int64_t)local[2] * matrix->m[2][0]) >> 16;
    y = ((int64_t)local[0] * matrix->m[0][1] +
         (int64_t)local[1] * matrix->m[1][1] +
         (int64_t)local[2] * matrix->m[2][1]) >> 16;
    z = ((int64_t)local[0] * matrix->m[0][2] +
         (int64_t)local[1] * matrix->m[1][2] +
         (int64_t)local[2] * matrix->m[2][2]) >> 16;
    x += matrix->m[3][0];
    y += matrix->m[3][1];
    z += matrix->m[3][2];
    out[0] = (int16_t)(x > INT16_MAX ? INT16_MAX : x < INT16_MIN ? INT16_MIN : x);
    out[1] = (int16_t)(y > INT16_MAX ? INT16_MAX : y < INT16_MIN ? INT16_MIN : y);
    out[2] = (int16_t)(z > INT16_MAX ? INT16_MAX : z < INT16_MIN ? INT16_MIN : z);
}

bool sm64_saturn_actor_pose_evaluate(
    const sm64_saturn_actor_bank_view_t *bank,
    int16_t animation_id, int16_t animation_frame,
    sm64_saturn_actor_pose_work_t *work,
    sm64_saturn_actor_pose_view_t *pose)
{
    sm64_saturn_actor_animation_record_t record;
    sm64_saturn_mtx_t *matrices;
    int32_t frame;
    uint16_t joint, vertex;

    if (bank == NULL || work == NULL || pose == NULL || work->vertices == NULL ||
        work->joint_matrices_q16 == NULL || animation_id < 0 ||
        (uint16_t)animation_id >= SM64_SATURN_MARIO_ANIMATION_COUNT ||
        (uint16_t)animation_id >= bank->bank.animation_count ||
        work->vertex_capacity < bank->bank.vertex_count ||
        work->joint_capacity < bank->bank.joint_count ||
        (work->light_intensity != NULL &&
         work->light_capacity < bank->bank.vertex_count) ||
        !sm64_saturn_actor_bank_animation(bank, (uint16_t)animation_id, &record) ||
        record.frame_count == 0U)
        return false;

    frame = clamp_s16_frame(animation_frame);
    if ((uint32_t)frame >= record.frame_count)
        frame = (int32_t)record.frame_count - 1;
    memset(pose, 0, sizeof(*pose));
    pose->animation_id = (uint16_t)animation_id;
    pose->frame = (uint16_t)frame;
    pose->frame_count = record.frame_count;
    pose->vertex_count = bank->bank.vertex_count;
    pose->joint_count = bank->bank.joint_count;
    for (uint16_t axis = 0U; axis < 3U; axis++) {
        int16_t sample = 0;
        if (!sm64_saturn_actor_bank_sample_channel(
                bank, (uint16_t)animation_id, (uint16_t)frame, axis, &sample)) {
            return false;
        }
        pose->root_translation[axis] = sample;
    }

    matrices = (sm64_saturn_mtx_t *)(void *)work->joint_matrices_q16;
    for (joint = 0U; joint < bank->bank.joint_count; joint++) {
        sm64_saturn_actor_joint_t source_joint;
        int16_t angles[3] = {0, 0, 0};
        int32_t translation[3];
        sm64_saturn_mtx_t local;
        if (!sm64_saturn_actor_bank_joint(bank, joint, &source_joint) ||
            source_joint.parent_ordinal >= (int16_t)joint ||
            source_joint.parent_ordinal < -1) {
            return false;
        }
        for (uint16_t axis = 0U; axis < 3U; axis++) {
            int16_t sample = 0;
            const uint16_t channel = (uint16_t)(3U + joint * 3U + axis);
            if (!sm64_saturn_actor_bank_sample_channel(
                    bank, (uint16_t)animation_id, (uint16_t)frame,
                    channel, &sample)) {
                return false;
            }
            angles[axis] = sample;
            translation[axis] = (int32_t)source_joint.translation[axis] << 16;
        }
        if (joint == 0U) {
            translation[0] += (int32_t)pose->root_translation[0] << 16;
            translation[1] += (int32_t)pose->root_translation[1] << 16;
            translation[2] += (int32_t)pose->root_translation[2] << 16;
        }
        sm64_saturn_mtxq_rotate_xyz_and_translate(
            &local, translation, angles[0], angles[1], angles[2]);
        if (source_joint.parent_ordinal < 0) {
            matrices[joint] = local;
        } else {
            if ((uint16_t)source_joint.parent_ordinal >= joint) {
                return false;
            }
            /* The validated compact bank bounds source translations and the
             * fixed-point constructor bounds rotations.  The helper's bool
             * is diagnostic overflow telemetry for general scene matrices;
             * actor poses retain their bounded result and fail closed only on
             * malformed bank spans above. */
            (void)sm64_saturn_matrix_mul(
                &local, &matrices[source_joint.parent_ordinal], &matrices[joint]);
        }
    }
    for (vertex = 0U; vertex < bank->bank.vertex_count; vertex++) {
        sm64_saturn_actor_vertex_t source_vertex;
        if (!sm64_saturn_actor_bank_vertex(bank, vertex, &source_vertex) ||
            source_vertex.joint_ordinal >= bank->bank.joint_count) {
            return false;
        }
        matrix_apply(&matrices[source_vertex.joint_ordinal], source_vertex.local,
                     work->vertices[vertex]);
        if (work->light_intensity != NULL)
            work->light_intensity[vertex] = SM64_SATURN_ACTOR_POSE_LIGHT_DEFAULT;
    }
    pose->vertices = (const int16_t (*)[3])work->vertices;
    pose->light_intensity = work->light_intensity;
    return true;
}
