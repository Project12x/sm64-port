/*
 * Task 4 live-Mario bridge.
 *
 * The generated mesh/pose bank is promoted from the M2 turntable unchanged;
 * this file only selects a source pose from the real engine's read-only
 * graph-node state.  The walking bank is intentionally used for the current
 * walking-family staging set; other animation IDs use the generated neutral
 * source animation until their source pose bank is promoted.
 */
#include "saturn_actor_bridge.h"
#include "saturn_render_native_math.h"

#include "game/level_update.h"
#include "game/camera.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "graph_node.h"
#include "mario_animation_ids.h"
#include "object_fields.h"

#include "saturn_mario_actor_mesh.h"

static uint8_t is_walking_family(int16_t animation_id)
{
    switch (animation_id) {
    case MARIO_ANIM_WALKING:
    case MARIO_ANIM_WALK_WITH_LIGHT_OBJ:
    case MARIO_ANIM_RUN_WITH_LIGHT_OBJ:
    case MARIO_ANIM_SLOW_WALK_WITH_LIGHT_OBJ:
    case MARIO_ANIM_WALK_PANTING:
    case MARIO_ANIM_WALK_WITH_HEAVY_OBJ:
        return 1U;
    default:
        return 0U;
    }
}

uint8_t sm64_saturn_mario_actor_snapshot(
    sm64_saturn_mario_actor_snapshot_t *snapshot)
{
    if (snapshot == NULL || gMarioState == NULL) {
        return 0U;
    }
    snapshot->position[0] = sm64_saturn_world_unit_from_float(gMarioState->pos[0]);
    snapshot->position[1] = sm64_saturn_world_unit_from_float(gMarioState->pos[1]);
    snapshot->position[2] = sm64_saturn_world_unit_from_float(gMarioState->pos[2]);
    snapshot->camera_position[0] = sm64_saturn_world_unit_from_float(gLakituState.pos[0]);
    snapshot->camera_position[1] = sm64_saturn_world_unit_from_float(gLakituState.pos[1]);
    snapshot->camera_position[2] = sm64_saturn_world_unit_from_float(gLakituState.pos[2]);
    snapshot->camera_focus[0] = sm64_saturn_world_unit_from_float(gLakituState.focus[0]);
    snapshot->camera_focus[1] = sm64_saturn_world_unit_from_float(gLakituState.focus[1]);
    snapshot->camera_focus[2] = sm64_saturn_world_unit_from_float(gLakituState.focus[2]);
    snapshot->camera_mode = gLakituState.mode;
    snapshot->camera_yaw = gLakituState.yaw;
    snapshot->camera_pitch = gLakituState.oldPitch;
    snapshot->yaw = gMarioState->faceAngle[1];
    snapshot->action = gMarioState->action;
    /* The simulation state is authoritative and can outlive its graph object
     * during level/bootstrap transitions. Keep the actor seam live in that
     * interval by using the neutral generated pose; once the graph object is
     * present again its animation id/frame are consumed verbatim. */
    if (gMarioObject != NULL) {
        snapshot->animation_id = gMarioObject->header.gfx.animInfo.animID;
        snapshot->animation_frame = gMarioObject->header.gfx.animInfo.animFrame;
        snapshot->area_index = gMarioObject->header.gfx.areaIndex;
    } else {
        snapshot->animation_id = 0;
        snapshot->animation_frame = 0;
        snapshot->area_index = -1;
    }
    snapshot->walking_bank = is_walking_family(snapshot->animation_id);
    snapshot->valid = 1U;
    return 1U;
}

uint8_t sm64_saturn_mario_actor_pose(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_actor_pose_t *pose)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid) {
        return 0U;
    }
    pose->walking_bank = snapshot->walking_bank;
    pose->vertex_count = SM64_MARIO_VERTEX_COUNT;
    if (snapshot->walking_bank) {
        int32_t frame = snapshot->animation_frame;
        if (frame < 0) frame = 0;
        pose->frame_count = SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT;
        pose->frame = (uint16_t)((uint32_t)frame % pose->frame_count);
        pose->vertices = sm64_mario_walking_animation_vertices[pose->frame];
        pose->light_intensity =
            sm64_mario_walking_animation_light_intensity[pose->frame];
    } else {
        int32_t frame = snapshot->animation_frame;
        if (frame < 0) frame = 0;
        pose->frame_count = SM64_MARIO_ANIMATION_FRAME_COUNT;
        pose->frame = (uint16_t)((uint32_t)frame % pose->frame_count);
        pose->vertices = sm64_mario_animation_vertices[pose->frame];
        pose->light_intensity = sm64_mario_animation_light_intensity[pose->frame];
    }
    return 1U;
}

uint8_t sm64_saturn_mario_actor_pose_selector(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_pose_selector_t *selector)
{
    sm64_saturn_mario_actor_pose_t pose;

    if (selector == NULL) return 0U;
    selector->vertex_bank_id = 0U;
    selector->material_bank_id = 0U;
    selector->frame = 0U;
    selector->frame_count = 0U;
    selector->vertex_count = 0U;
    selector->walking_bank = 0U;
    selector->valid = 0U;
    if (!sm64_saturn_mario_actor_pose(snapshot, &pose)) return 0U;
    selector->vertex_bank_id = pose.walking_bank ?
        SM64_SATURN_MARIO_VERTEX_BANK_WALKING :
        SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL;
    selector->material_bank_id = SM64_SATURN_MARIO_MATERIAL_BANK_DEFAULT;
    selector->frame = pose.frame;
    selector->frame_count = pose.frame_count;
    selector->vertex_count = pose.vertex_count;
    selector->walking_bank = pose.walking_bank;
    selector->valid = 1U;
    return 1U;
}
