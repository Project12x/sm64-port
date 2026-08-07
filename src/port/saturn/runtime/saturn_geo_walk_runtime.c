/* NULL (used throughout this file) is not guaranteed by
 * saturn_geo_walk_runtime.h's <stdbool.h>/<stdint.h> includes -- same class
 * of host-only-masked missing include Task 9 found in saturn_hud_layout.c/
 * saturn_hud_publish.c during this plan's first real SH-2 cross-compile. */
#include <stddef.h>

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

bool sm64_saturn_geo_walk_runtime_run(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t root,
    const sm64_saturn_geo_walk_runtime_ops_t *ops,
    void *user)
{
    sm64_saturn_geo_walk_runtime_frame_t event;

    if (walk == NULL || ops == NULL || ops->enter == NULL ||
        ops->dispatch == NULL || ops->leave == NULL) {
        return false;
    }
    if (root == 0U) return true;
    if (!push(walk, root, 0U, SM64_SATURN_GEO_WALK_RUNTIME_ENTER,
              0U, 0U, 0U)) return false;

    while (sm64_saturn_geo_walk_runtime_next(walk, &event)) {
        sm64_saturn_geo_walk_runtime_enter_t result = { 0 };
        if (event.phase == SM64_SATURN_GEO_WALK_RUNTIME_ENTER) {
            if (!ops->enter(event.node, &result, user)) return false;
            if (!result.admitted) {
                if (result.sibling != 0U &&
                    !push(walk, result.sibling, 0U,
                          SM64_SATURN_GEO_WALK_RUNTIME_ENTER, 0U, 0U, 0U)) {
                    return false;
                }
                continue;
            }
            /* The sibling continuation is below the entire child subtree
             * (and, when present, the second_child subtree below it). */
            if (result.sibling != 0U &&
                !push(walk, result.sibling, 0U,
                      SM64_SATURN_GEO_WALK_RUNTIME_ENTER, 0U, 0U, 0U)) {
                return false;
            }
            if (result.child != 0U && result.second_child != 0U) {
                /* Two-subtree node: walk child, optional boundary action,
                 * walk second_child, optional final leave. Frames are
                 * pushed bottom-to-top in the reverse of the desired
                 * pop/fire order (LIFO), so the eventual pop order is:
                 * child subtree drains, dispatch (if deferred), boundary
                 * leave (if boundary_required), second_child subtree
                 * drains, final leave (if leave_required), sibling. See
                 * saturn_geo_walk_runtime.h's
                 * sm64_saturn_geo_walk_runtime_enter_t comments for the
                 * exact contract. second_child is ignored (this branch is
                 * skipped) whenever child == 0 -- there is no meaningful
                 * "second" subtree without a first. */
                if (result.leave_required &&
                    !push(walk, event.node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_LEAVE,
                          result.leave_action, result.matrix_depth,
                          result.context_token)) {
                    return false;
                }
                if (!push(walk, result.second_child, 0U,
                          SM64_SATURN_GEO_WALK_RUNTIME_ENTER, 0U,
                          result.matrix_depth, result.context_token)) {
                    return false;
                }
                if (result.boundary_required &&
                    !push(walk, event.node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_LEAVE,
                          result.boundary_action, result.matrix_depth,
                          result.context_token)) {
                    return false;
                }
                if (result.defer_dispatch &&
                    !push(walk, event.node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_DISPATCH,
                          0U, 0U, result.context_token)) {
                    return false;
                }
                if (!push(walk, result.child, 0U,
                          SM64_SATURN_GEO_WALK_RUNTIME_ENTER, 0U,
                          result.matrix_depth, result.context_token)) {
                    return false;
                }
            } else {
                if (result.leave_required &&
                    !push(walk, event.node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_LEAVE,
                          result.leave_action, result.matrix_depth,
                          result.context_token)) {
                    return false;
                }
                if (result.defer_dispatch && result.child != 0U &&
                    !push(walk, event.node, 0U, SM64_SATURN_GEO_WALK_RUNTIME_DISPATCH,
                          0U, 0U, result.context_token)) {
                    return false;
                }
                if (result.child != 0U &&
                    !push(walk, result.child, 0U,
                          SM64_SATURN_GEO_WALK_RUNTIME_ENTER, 0U,
                          result.matrix_depth, result.context_token)) {
                    return false;
                }
                if (result.child == 0U && result.defer_dispatch) {
                    ops->dispatch(event.node, user);
                }
            }
        } else if (event.phase == SM64_SATURN_GEO_WALK_RUNTIME_DISPATCH) {
            ops->dispatch(event.node, user);
        } else if (event.phase == SM64_SATURN_GEO_WALK_RUNTIME_LEAVE) {
            ops->leave(event.node, event.leave_action, event.matrix_depth,
                       event.context_token, user);
        } else {
            return false;
        }
        if (walk->fail_reason != SM64_SATURN_GEO_WALK_RUNTIME_NONE) return false;
    }
    return walk->fail_reason == SM64_SATURN_GEO_WALK_RUNTIME_NONE;
}
