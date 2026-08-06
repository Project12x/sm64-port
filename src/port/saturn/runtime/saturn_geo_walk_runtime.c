#include "saturn_geo_walk_runtime.h"

static bool push(sm64_saturn_geo_walk_runtime_t *walk,
                 uintptr_t node,
                 uintptr_t sibling,
                 sm64_saturn_geo_walk_runtime_phase_t phase,
                 uint16_t leave_action,
                 uint16_t matrix_depth,
                 uint16_t context_token)
{
    sm64_saturn_geo_walk_runtime_frame_t *frame;

    if (walk == NULL || walk->frames == NULL || walk->fail_reason !=
        SM64_SATURN_GEO_WALK_RUNTIME_NONE || walk->depth >= walk->capacity) {
        if (walk != NULL && walk->fail_reason ==
            SM64_SATURN_GEO_WALK_RUNTIME_NONE) {
            walk->overflowed = true;
            walk->fail_reason = SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW;
        }
        return false;
    }
    frame = &walk->frames[walk->depth++];
    frame->node = node;
    frame->sibling = sibling;
    frame->phase = (uint16_t)phase;
    frame->leave_action = leave_action;
    frame->matrix_depth = matrix_depth;
    frame->context_token = context_token;
    if (walk->depth > walk->high_water) walk->high_water = walk->depth;
    return true;
}

void sm64_saturn_geo_walk_runtime_init(
    sm64_saturn_geo_walk_runtime_t *walk,
    sm64_saturn_geo_walk_runtime_frame_t *frames,
    uint16_t capacity)
{
    if (walk == NULL) return;
    walk->frames = frames;
    walk->capacity = capacity;
    walk->depth = 0U;
    walk->high_water = 0U;
    walk->reserved = 0U;
    walk->overflowed = false;
    walk->fail_reason = SM64_SATURN_GEO_WALK_RUNTIME_NONE;
    walk->generation = 0U;
    walk->scene_id = 0U;
}

bool sm64_saturn_geo_walk_runtime_push_enter(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t node,
    uintptr_t sibling,
    uint16_t matrix_depth,
    uint16_t context_token)
{
    return push(walk, node, sibling, SM64_SATURN_GEO_WALK_RUNTIME_ENTER,
                0U, matrix_depth, context_token);
}

bool sm64_saturn_geo_walk_runtime_push_leave(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t node,
    uint16_t leave_action,
    uint16_t context_token)
{
    return push(walk, node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_LEAVE,
                leave_action, 0U, context_token);
}

bool sm64_saturn_geo_walk_runtime_next(
    sm64_saturn_geo_walk_runtime_t *walk,
    sm64_saturn_geo_walk_runtime_frame_t *event)
{
    if (walk == NULL || event == NULL || walk->fail_reason !=
        SM64_SATURN_GEO_WALK_RUNTIME_NONE || walk->depth == 0U) return false;
    *event = walk->frames[--walk->depth];
    return true;
}
