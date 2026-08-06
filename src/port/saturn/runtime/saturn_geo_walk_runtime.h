#ifndef SM64_SATURN_GEO_WALK_RUNTIME_H
#define SM64_SATURN_GEO_WALK_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sm64_saturn_geo_walk_runtime_phase {
    SM64_SATURN_GEO_WALK_RUNTIME_ENTER = 1U,
    SM64_SATURN_GEO_WALK_RUNTIME_LEAVE = 2U,
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

#endif
