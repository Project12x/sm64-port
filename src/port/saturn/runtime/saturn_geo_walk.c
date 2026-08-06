#include "saturn_geo_walk.h"

#include <stddef.h>

static bool push(sm64_saturn_geo_walk_t *walk,
                 uintptr_t node,
                 sm64_saturn_geo_walk_phase_t phase,
                 uint16_t leave_action,
                 uint16_t matrix_depth,
                 uint16_t context_token)
{
    sm64_saturn_geo_walk_frame_t *frame;

    if (walk == NULL || walk->frames == NULL || walk->fail_reason !=
        SM64_SATURN_GEO_WALK_FAIL_NONE || walk->depth >= walk->capacity) {
        if (walk != NULL && walk->fail_reason ==
            SM64_SATURN_GEO_WALK_FAIL_NONE) {
            walk->overflowed = true;
            walk->fail_reason = SM64_SATURN_GEO_WALK_OVERFLOW;
        }
        return false;
    }

    frame = &walk->frames[walk->depth++];
    frame->node = node;
    frame->phase = (uint16_t)phase;
    frame->leave_action = leave_action;
    frame->matrix_depth = matrix_depth;
    frame->context_token = context_token;
    if (walk->depth > walk->high_water) walk->high_water = walk->depth;
    return true;
}

void sm64_saturn_geo_walk_init(sm64_saturn_geo_walk_t *walk,
                               sm64_saturn_geo_walk_frame_t *frames,
                               uint16_t capacity)
{
    if (walk == NULL) return;
    walk->frames = frames;
    walk->capacity = capacity;
    walk->depth = 0U;
    walk->high_water = 0U;
    walk->reserved = 0U;
    walk->overflowed = false;
    walk->fail_reason = SM64_SATURN_GEO_WALK_FAIL_NONE;
    walk->generation = 0U;
    walk->scene_id = 0U;
}

bool sm64_saturn_geo_walk_push_enter(sm64_saturn_geo_walk_t *walk,
                                     uintptr_t node,
                                     uint16_t leave_action,
                                     uint16_t matrix_depth,
                                     uint16_t context_token)
{
    return push(walk, node, SM64_SATURN_GEO_WALK_ENTER, leave_action,
                matrix_depth, context_token);
}

bool sm64_saturn_geo_walk_push_leave(sm64_saturn_geo_walk_t *walk,
                                     uintptr_t node,
                                     uint16_t leave_action,
                                     uint16_t context_token)
{
    return push(walk, node, SM64_SATURN_GEO_WALK_LEAVE, leave_action, 0U,
                context_token);
}

bool sm64_saturn_geo_walk_next(sm64_saturn_geo_walk_t *walk,
                               sm64_saturn_geo_walk_event_t *event)
{
    if (walk == NULL || event == NULL || walk->fail_reason !=
        SM64_SATURN_GEO_WALK_FAIL_NONE || walk->depth == 0U) return false;
    *event = walk->frames[--walk->depth];
    return true;
}

sm64_saturn_geo_walk_fail_reason_t
sm64_saturn_geo_walk_fail_reason(const sm64_saturn_geo_walk_t *walk)
{
    return walk == NULL ? SM64_SATURN_GEO_WALK_OVERFLOW : walk->fail_reason;
}
