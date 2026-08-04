/* Read-only bridge from authoritative SM64 Mario state to the actor IR. */
#ifndef SM64_SATURN_ACTOR_BRIDGE_H
#define SM64_SATURN_ACTOR_BRIDGE_H

#include <stdint.h>

typedef struct sm64_saturn_mario_actor_snapshot {
    int32_t position[3];
    int32_t camera_position[3];
    int32_t camera_focus[3];
    int16_t yaw;
    uint32_t action;
    int16_t animation_id;
    int16_t animation_frame;
    int8_t area_index;
    uint8_t camera_mode;
    int16_t camera_yaw;
    int16_t camera_pitch;
    uint8_t walking_bank;
    uint8_t valid;
} sm64_saturn_mario_actor_snapshot_t;

typedef struct sm64_saturn_mario_actor_pose {
    const int16_t (*vertices)[3];
    const uint8_t *light_intensity;
    uint16_t frame;
    uint16_t frame_count;
    uint16_t vertex_count;
    uint8_t walking_bank;
} sm64_saturn_mario_actor_pose_t;

typedef struct sm64_saturn_mario_pose_selector {
    uint32_t vertex_bank_id;
    uint32_t material_bank_id;
    uint16_t frame;
    uint16_t frame_count;
    uint16_t vertex_count;
    uint8_t walking_bank;
    uint8_t valid;
} sm64_saturn_mario_pose_selector_t;

enum {
    SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL = 1U,
    SM64_SATURN_MARIO_VERTEX_BANK_WALKING = 2U,
    SM64_SATURN_MARIO_MATERIAL_BANK_DEFAULT = 1U,
};

/* Reads only gMarioState/gMarioObject; it never writes simulation state. */
uint8_t sm64_saturn_mario_actor_snapshot(
    sm64_saturn_mario_actor_snapshot_t *snapshot);

/* Select the generated source-pose bank for a snapshot. */
uint8_t sm64_saturn_mario_actor_pose(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_actor_pose_t *pose);

uint8_t sm64_saturn_mario_actor_pose_selector(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_pose_selector_t *selector);

#endif
