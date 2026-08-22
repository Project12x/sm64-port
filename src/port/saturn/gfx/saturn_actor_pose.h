#ifndef SM64_SATURN_ACTOR_POSE_H
#define SM64_SATURN_ACTOR_POSE_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_bank.h"

#define SM64_SATURN_MARIO_ANIMATION_COUNT 209U
#define SM64_SATURN_ACTOR_POSE_LIGHT_DEFAULT 255U

typedef struct sm64_saturn_actor_pose_work {
    int16_t (*vertices)[3];
    uint8_t *light_intensity;
    int32_t *joint_matrices_q16;
    uint16_t vertex_capacity;
    uint16_t joint_capacity;
    uint16_t light_capacity;
} sm64_saturn_actor_pose_work_t;

typedef struct sm64_saturn_actor_pose_view {
    const int16_t (*vertices)[3];
    const uint8_t *light_intensity;
    uint16_t animation_id;
    uint16_t frame;
    uint16_t frame_count;
    uint16_t vertex_count;
    uint16_t joint_count;
    int16_t root_translation[3];
} sm64_saturn_actor_pose_view_t;

/* Evaluate exactly one source-selected pose.  The caller owns all scratch,
 * and Saturn never advances an animation frame here; frame/id are inputs from
 * the authoritative source geo tick. */
bool sm64_saturn_actor_pose_evaluate(
    const sm64_saturn_actor_bank_view_t *bank,
    int16_t animation_id, int16_t animation_frame,
    sm64_saturn_actor_pose_work_t *work,
    sm64_saturn_actor_pose_view_t *pose);

/* Same evaluation, with the two per-frame-invariant bank slices supplied by
 * the caller instead of decoded out of the bank bytes on every frame.  The
 * actor bank itself lives in CART, so the vertex and joint tables would
 * otherwise be re-read across the A-bus 424 and 20 times per frame even
 * though neither ever changes.  Passing NULL for either table restores the
 * in-bank read for that table, so this is a strict superset of
 * `sm64_saturn_actor_pose_evaluate`.  Staged tables must hold at least
 * `bank->bank.vertex_count` / `bank->bank.joint_count` entries and must be a
 * faithful copy of the bank's own records; they are trusted, not revalidated,
 * because the boot-time staging pass validated them once. */
bool sm64_saturn_actor_pose_evaluate_staged(
    const sm64_saturn_actor_bank_view_t *bank,
    int16_t animation_id, int16_t animation_frame,
    sm64_saturn_actor_pose_work_t *work,
    const sm64_saturn_actor_vertex_t *staged_vertices,
    const sm64_saturn_actor_joint_t *staged_joints,
    sm64_saturn_actor_pose_view_t *pose);

#endif
