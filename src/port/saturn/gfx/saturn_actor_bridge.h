/* Read-only bridge from authoritative SM64 Mario state to the actor IR. */
#ifndef SM64_SATURN_ACTOR_BRIDGE_H
#define SM64_SATURN_ACTOR_BRIDGE_H

#include <stdint.h>

typedef struct sm64_saturn_mario_actor_snapshot {
    float position[3];
    int16_t yaw;
    uint32_t action;
    int16_t animation_id;
    int16_t animation_frame;
    int8_t area_index;
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

/* Reads only gMarioState/gMarioObject; it never writes simulation state. */
uint8_t sm64_saturn_mario_actor_snapshot(
    sm64_saturn_mario_actor_snapshot_t *snapshot);

/* Select the generated source-pose bank for a snapshot. */
uint8_t sm64_saturn_mario_actor_pose(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_actor_pose_t *pose);

#endif
