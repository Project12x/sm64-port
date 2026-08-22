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

#include <string.h>

#include "game/level_update.h"
#include "game/camera.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "graph_node.h"
#include "mario_animation_ids.h"
#include "object_fields.h"

#include "saturn_mario_actor_mesh.h"

#ifndef SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
#define SATURN_FEATURE_COMPLETE_MARIO_ANIMATION 0
#endif

#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
extern const uint8_t sm64_saturn_mario_actor_bank_data[];
extern const uint32_t sm64_saturn_mario_actor_bank_size;
#define COMPLETE_ACTOR_POSE_SLOT_COUNT 2U
typedef struct complete_actor_pose_slot {
    int16_t vertices[SM64_MARIO_VERTEX_COUNT][3];
    uint8_t lights[SM64_MARIO_VERTEX_COUNT];
    int32_t joints[20U * 16U];
    sm64_saturn_actor_pose_view_t evaluated;
    uint16_t animation_id;
    uint16_t animation_frame;
    uint8_t valid;
} complete_actor_pose_slot_t;
static sm64_saturn_actor_bank_view_t complete_actor_bank;
static complete_actor_pose_slot_t complete_actor_pose_slots[
    COMPLETE_ACTOR_POSE_SLOT_COUNT];
static uint8_t complete_actor_next_pose_slot;
static uint8_t complete_actor_bank_ready;

/* The reduced bake keeps a record for every animation ID and aliases the ones
 * it does not bake onto their nearest baked neighbour, so no requested ID can
 * evaluate to nothing.  This ID is the last resort if the bank itself is
 * unusable for the requested ID: the neutral idle the =0 path already used.
 * It is always present in the bake list. */
#define COMPLETE_ACTOR_FALLBACK_ANIMATION_ID ((int16_t)MARIO_ANIM_IDLE_HEAD_CENTER)

/* Per-frame-invariant bank slices, staged once out of CART into LWRAM.  The
 * bank lives in `.cart_rodata`; without this the pose evaluator re-reads 424
 * vertex records and 20 joint records across the A-bus every frame for data
 * that never changes.  HWRAM has 2,552 B of slack and LWRAM has 92,704 B
 * free, so the copy is funded from the idle pool. */
static sm64_saturn_actor_vertex_t complete_actor_staged_vertices[
    SM64_MARIO_VERTEX_COUNT]
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static sm64_saturn_actor_joint_t complete_actor_staged_joints[20U]
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint8_t complete_actor_slices_staged;

/* Live-observable evidence for the reduced bake and the LWRAM staging.  Kept
 * in LWRAM so it costs no HWRAM, and `used` so --gc-sections cannot discard
 * it.  `probe_*` are filled once from the shipped bank; the counters are the
 * proof that the per-frame path never needed the last-resort fallback. */
#define COMPLETE_ACTOR_PROBE_ID_COUNT 4U
typedef struct sm64_saturn_complete_actor_probe {
    uint32_t magic;
    uint32_t probe_values_offset[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t probe_animation_id[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t probe_frame_count[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t bank_animation_count;
    uint16_t bank_vertex_count;
    uint16_t staged_slices;
    uint16_t evaluate_calls;
    uint16_t fallback_hits;
    uint16_t last_fallback_requested_id;
} sm64_saturn_complete_actor_probe_t;
sm64_saturn_complete_actor_probe_t sm64_saturn_complete_actor_probe
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static const uint16_t complete_actor_probe_ids[COMPLETE_ACTOR_PROBE_ID_COUNT] = {
    29U,  /* MARIO_ANIM_CREDITS_WAVING  - not baked; aliases its neighbour */
    28U,  /* MARIO_ANIM_CLIMB_DOWN_LEDGE - baked; the expected alias target */
    195U, /* MARIO_ANIM_IDLE_HEAD_LEFT  - baked; requested by the route      */
    114U, /* MARIO_ANIM_RUNNING         - baked; requested by the route      */
};

static void complete_actor_stage_slices(void)
{
    uint16_t index;
    if (complete_actor_bank.bank.vertex_count > SM64_MARIO_VERTEX_COUNT ||
        complete_actor_bank.bank.joint_count > 20U)
        return;
    for (index = 0U; index < complete_actor_bank.bank.vertex_count; index++) {
        if (!sm64_saturn_actor_bank_vertex(&complete_actor_bank, index,
                                           &complete_actor_staged_vertices[index]) ||
            complete_actor_staged_vertices[index].joint_ordinal >=
                complete_actor_bank.bank.joint_count)
            return;
    }
    for (index = 0U; index < complete_actor_bank.bank.joint_count; index++) {
        if (!sm64_saturn_actor_bank_joint(&complete_actor_bank, index,
                                          &complete_actor_staged_joints[index]) ||
            complete_actor_staged_joints[index].parent_ordinal >= (int16_t)index ||
            complete_actor_staged_joints[index].parent_ordinal < -1)
            return;
    }
    complete_actor_slices_staged = 1U;
}

static void complete_actor_publish_probe(void)
{
    sm64_saturn_complete_actor_probe.bank_animation_count =
        complete_actor_bank.bank.animation_count;
    sm64_saturn_complete_actor_probe.bank_vertex_count =
        complete_actor_bank.bank.vertex_count;
    sm64_saturn_complete_actor_probe.staged_slices = complete_actor_slices_staged;
    for (uint16_t index = 0U; index < COMPLETE_ACTOR_PROBE_ID_COUNT; index++) {
        sm64_saturn_actor_animation_record_t record;
        sm64_saturn_complete_actor_probe.probe_animation_id[index] =
            complete_actor_probe_ids[index];
        if (!sm64_saturn_actor_bank_animation(&complete_actor_bank,
                                              complete_actor_probe_ids[index],
                                              &record))
            continue;
        sm64_saturn_complete_actor_probe.probe_frame_count[index] =
            record.frame_count;
        sm64_saturn_complete_actor_probe.probe_values_offset[index] =
            record.values_offset;
    }
    sm64_saturn_complete_actor_probe.magic = 0x53414E4DUL; /* 'SANM' */
}

static const sm64_saturn_actor_bank_view_t *complete_bank(void)
{
    if (complete_actor_bank_ready == 0U) {
        complete_actor_bank_ready = (uint8_t)sm64_saturn_actor_bank_validate(
            sm64_saturn_mario_actor_bank_data, sm64_saturn_mario_actor_bank_size,
            &complete_actor_bank);
        if (complete_actor_bank_ready != 0U) {
            complete_actor_stage_slices();
            complete_actor_publish_probe();
        }
    }
    return complete_actor_bank_ready != 0U ? &complete_actor_bank : NULL;
}

static uint8_t complete_actor_evaluate(
    const sm64_saturn_mario_actor_snapshot_t *snapshot, uint8_t slot_index)
{
    complete_actor_pose_slot_t *const slot =
        &complete_actor_pose_slots[slot_index];
    sm64_saturn_actor_pose_work_t work = {
        .vertices = slot->vertices,
        .light_intensity = slot->lights,
        .joint_matrices_q16 = slot->joints,
        .vertex_capacity = SM64_MARIO_VERTEX_COUNT,
        .joint_capacity = 20U,
        .light_capacity = SM64_MARIO_VERTEX_COUNT,
    };
    const sm64_saturn_actor_bank_view_t *const bank = complete_bank();
    const sm64_saturn_actor_vertex_t *const staged_vertices =
        complete_actor_slices_staged != 0U ? complete_actor_staged_vertices : NULL;
    const sm64_saturn_actor_joint_t *const staged_joints =
        complete_actor_slices_staged != 0U ? complete_actor_staged_joints : NULL;
    sm64_saturn_complete_actor_probe.evaluate_calls++;
    if (bank == NULL) {
        slot->valid = 0U;
        return 0U;
    }
    if (!sm64_saturn_actor_pose_evaluate_staged(
            bank, snapshot->animation_id, snapshot->animation_frame,
            &work, staged_vertices, staged_joints, &slot->evaluated)) {
        /* Constitution 6: degrade at the smallest safe unit.  The reduced
         * bake already aliases every unbaked ID onto a baked neighbour, so
         * reaching here means the bank cannot serve the requested ID at all.
         * Show the neutral idle at the requested frame rather than removing
         * Mario for the frame -- a wrong pose is a degradation, a missing
         * Mario is a pop.  The requested frame is carried through so the
         * fallback still advances instead of freezing. */
        sm64_saturn_complete_actor_probe.fallback_hits++;
        sm64_saturn_complete_actor_probe.last_fallback_requested_id =
            (uint16_t)snapshot->animation_id;
        if (!sm64_saturn_actor_pose_evaluate_staged(
                bank, COMPLETE_ACTOR_FALLBACK_ANIMATION_ID,
                snapshot->animation_frame, &work, staged_vertices,
                staged_joints, &slot->evaluated)) {
            slot->valid = 0U;
            return 0U;
        }
    }
    /* The generic pose evaluator correctly transforms the complete bank but
     * deliberately initializes its lighting workspace uniformly.  Preserve
     * the existing, source-generated per-vertex Gouraud contract here until
     * the complete-animation bank carries its own matching light stream. */
    memcpy(slot->lights,
           sm64_mario_animation_light_intensity[
               slot->evaluated.frame % SM64_MARIO_ANIMATION_FRAME_COUNT],
           sizeof(slot->lights));
    slot->animation_id = slot->evaluated.animation_id;
    slot->animation_frame = slot->evaluated.frame;
    slot->valid = 1U;
    return 1U;
}
#endif

#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
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
#endif

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
#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    snapshot->walking_bank = is_walking_family(snapshot->animation_id);
#endif
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
    memset(pose, 0, sizeof(*pose));
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    {
        const uint8_t slot = complete_actor_next_pose_slot++ %
                             COMPLETE_ACTOR_POSE_SLOT_COUNT;
        if (!complete_actor_evaluate(snapshot, slot))
            return 0U;
        pose->vertices = complete_actor_pose_slots[slot].evaluated.vertices;
        pose->light_intensity =
            complete_actor_pose_slots[slot].evaluated.light_intensity;
        pose->frame = complete_actor_pose_slots[slot].evaluated.frame;
        pose->frame_count = complete_actor_pose_slots[slot].evaluated.frame_count;
        pose->vertex_count = complete_actor_pose_slots[slot].evaluated.vertex_count;
        return 1U;
    }
#else
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
#endif
}

uint8_t sm64_saturn_mario_actor_pose_selector(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_pose_selector_t *selector)
{
    sm64_saturn_mario_actor_pose_t pose;

    if (selector == NULL) return 0U;
    memset(selector, 0, sizeof(*selector));
    selector->vertex_bank_id = 0U;
    selector->material_bank_id = 0U;
    selector->frame = 0U;
    selector->frame_count = 0U;
    selector->vertex_count = 0U;
    if (!sm64_saturn_mario_actor_pose(snapshot, &pose)) return 0U;
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    selector->vertex_bank_id = SM64_SATURN_MARIO_VERTEX_BANK_COMPLETE;
    selector->pose_slot = (uint8_t)((complete_actor_next_pose_slot +
                                     COMPLETE_ACTOR_POSE_SLOT_COUNT - 1U) %
                                    COMPLETE_ACTOR_POSE_SLOT_COUNT);
#else
    selector->walking_bank = 0U;
    selector->vertex_bank_id = pose.walking_bank ?
        SM64_SATURN_MARIO_VERTEX_BANK_WALKING :
        SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL;
#endif
    selector->material_bank_id = SM64_SATURN_MARIO_MATERIAL_BANK_DEFAULT;
    selector->frame = pose.frame;
    selector->frame_count = pose.frame_count;
    selector->vertex_count = pose.vertex_count;
#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    selector->walking_bank = pose.walking_bank;
#endif
    selector->valid = 1U;
    return 1U;
}

uint8_t sm64_saturn_mario_actor_pose_from_selector(
    const sm64_saturn_mario_pose_selector_t *selector,
    sm64_saturn_mario_actor_pose_t *pose)
{
    if (selector == NULL || pose == NULL || selector->valid == 0U) return 0U;
    memset(pose, 0, sizeof(*pose));
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    if (selector->pose_slot >= COMPLETE_ACTOR_POSE_SLOT_COUNT ||
        !complete_actor_pose_slots[selector->pose_slot].valid)
        return 0U;
    pose->vertices = complete_actor_pose_slots[selector->pose_slot].evaluated.vertices;
    pose->light_intensity =
        complete_actor_pose_slots[selector->pose_slot].evaluated.light_intensity;
    pose->frame = complete_actor_pose_slots[selector->pose_slot].evaluated.frame;
    pose->frame_count =
        complete_actor_pose_slots[selector->pose_slot].evaluated.frame_count;
    pose->vertex_count =
        complete_actor_pose_slots[selector->pose_slot].evaluated.vertex_count;
    return 1U;
#else
    return 0U;
#endif
}
