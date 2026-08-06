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

#endif
