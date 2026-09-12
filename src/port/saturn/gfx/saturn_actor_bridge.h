/* Read-only bridge from authoritative SM64 Mario state to the actor IR. */
#ifndef SM64_SATURN_ACTOR_BRIDGE_H
#define SM64_SATURN_ACTOR_BRIDGE_H

#include <stdint.h>

#include "saturn_actor_pose.h"

typedef struct sm64_saturn_mario_actor_snapshot {
    int32_t position[3];
    int32_t camera_position[3];
    int32_t camera_focus[3];
    int16_t yaw;
    /* gfx.angle[0] and [2]. The original builds an object's matrix with
     * mtxf_rotate_zxy_and_translate over all THREE angles
     * (rendering_graph_node.c:1769); carrying only yaw silently flattens every
     * tilted pose. Measured against the real mtxf_rotate_zxy_and_translate on
     * host: exact at pitch = roll = 0, and wrong by up to 160 world units --
     * Mario's whole height -- at a quarter turn. That is the sudden-stop and
     * turn-around facing defect. */
    int16_t pitch;
    int16_t roll;
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
    uint8_t pose_slot;
} sm64_saturn_mario_pose_selector_t;

enum {
    SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL = 1U,
    SM64_SATURN_MARIO_VERTEX_BANK_WALKING = 2U,
    SM64_SATURN_MARIO_VERTEX_BANK_COMPLETE = 3U,
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

/* Return the immutable pose selected for a complete-animation snapshot.
 * Feature-on sourceboot uses this handoff so a render worker never rereads a
 * shared scratch buffer while the next generation is being evaluated. */
uint8_t sm64_saturn_mario_actor_pose_from_selector(
    const sm64_saturn_mario_pose_selector_t *selector,
    sm64_saturn_mario_actor_pose_t *pose);

/* Hand the resident S64F actor bundle to the bridge so the rigid-part lane
 * can be exercised against a real non-Mario bank at bring-up.  The bundle is
 * borrowed, never copied: the first enemy bank costs zero additional cart
 * bytes because it already ships inside the generic actor package.  Call once
 * per scene, before gameplay.  This does NOT wire a per-frame consumer. */
void sm64_saturn_complete_actor_set_enemy_bundle(
    const void *bytes, uint32_t byte_count,
    uint16_t family_ordinal, uint16_t model_id);

#endif
