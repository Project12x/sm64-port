#ifndef SM64_SATURN_GEO_WALK_RUNTIME_H
#define SM64_SATURN_GEO_WALK_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sm64_saturn_geo_walk_runtime_phase {
    SM64_SATURN_GEO_WALK_RUNTIME_ENTER = 1U,
    SM64_SATURN_GEO_WALK_RUNTIME_DISPATCH = 2U,
    SM64_SATURN_GEO_WALK_RUNTIME_LEAVE = 3U,
} sm64_saturn_geo_walk_runtime_phase_t;

typedef enum sm64_saturn_geo_walk_runtime_fail_reason {
    SM64_SATURN_GEO_WALK_RUNTIME_NONE = 0U,
    SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW = 1U,
} sm64_saturn_geo_walk_runtime_fail_reason_t;

typedef struct sm64_saturn_geo_walk_runtime_frame {
    uintptr_t node;
    uintptr_t sibling;
    uint16_t phase;
    uint16_t leave_action;
    uint16_t matrix_depth;
    uint16_t context_token;
} sm64_saturn_geo_walk_runtime_frame_t;

typedef struct sm64_saturn_geo_walk_runtime {
    sm64_saturn_geo_walk_runtime_frame_t *frames;
    uint16_t capacity;
    uint16_t depth;
    uint16_t high_water;
    uint16_t reserved;
    bool overflowed;
    sm64_saturn_geo_walk_runtime_fail_reason_t fail_reason;
    uint32_t generation;
    uint32_t scene_id;
} sm64_saturn_geo_walk_runtime_t;

typedef struct sm64_saturn_geo_walk_runtime_enter {
    uintptr_t child;
    uintptr_t sibling;
    uintptr_t second_child;      /* Walked strictly after child's entire
                                   * subtree (and dispatch, if deferred) has
                                   * drained. Ignored if child == 0 (there is
                                   * no meaningful "second" without a first).
                                   * A node needing only one child subtree
                                   * MUST leave this 0 -- existing wave 1/2
                                   * handlers are unaffected by construction. */
    uint16_t leave_action;       /* fires after EVERYTHING (child, dispatch,
                                   * second_child) drains, if leave_required */
    uint16_t boundary_action;    /* fires after child's subtree (and
                                   * dispatch) drains but before second_child
                                   * begins. Only pushed if second_child != 0
                                   * AND boundary_required is true. */
    uint16_t matrix_depth;
    uint16_t context_token;
    bool admitted;
    bool defer_dispatch;
    bool leave_required;
    bool boundary_required;
} sm64_saturn_geo_walk_runtime_enter_t;

typedef bool (*sm64_saturn_geo_walk_runtime_enter_fn)(
    uintptr_t node,
    sm64_saturn_geo_walk_runtime_enter_t *result,
    void *user);
typedef void (*sm64_saturn_geo_walk_runtime_dispatch_fn)(uintptr_t node, void *user);
typedef void (*sm64_saturn_geo_walk_runtime_leave_fn)(
    uintptr_t node,
    uint16_t leave_action,
    uint16_t matrix_depth,
    uint16_t context_token,
    void *user);

typedef struct sm64_saturn_geo_walk_runtime_ops {
    sm64_saturn_geo_walk_runtime_enter_fn enter;
    sm64_saturn_geo_walk_runtime_dispatch_fn dispatch;
    sm64_saturn_geo_walk_runtime_leave_fn leave;
} sm64_saturn_geo_walk_runtime_ops_t;

void sm64_saturn_geo_walk_runtime_init(
    sm64_saturn_geo_walk_runtime_t *walk,
    sm64_saturn_geo_walk_runtime_frame_t *frames,
    uint16_t capacity);

bool sm64_saturn_geo_walk_runtime_push_enter(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t node,
    uintptr_t sibling,
    uint16_t matrix_depth,
    uint16_t context_token);

bool sm64_saturn_geo_walk_runtime_push_leave(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t node,
    uint16_t leave_action,
    uint16_t context_token);

bool sm64_saturn_geo_walk_runtime_next(
    sm64_saturn_geo_walk_runtime_t *walk,
    sm64_saturn_geo_walk_runtime_frame_t *event);

bool sm64_saturn_geo_walk_runtime_run(
    sm64_saturn_geo_walk_runtime_t *walk,
    uintptr_t root,
    const sm64_saturn_geo_walk_runtime_ops_t *ops,
    void *user);

#endif
