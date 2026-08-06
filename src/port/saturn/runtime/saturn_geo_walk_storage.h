#ifndef SM64_SATURN_GEO_WALK_STORAGE_H
#define SM64_SATURN_GEO_WALK_STORAGE_H

#include <stdint.h>

#include "saturn_geo_walk.h"
#include "saturn_geo_depth_manifest.h"

/* This is the only production owner for iterative geo continuation frames.
 * It is CPU-only LWRAM state; VDP1/SCU queues and the slave stack must never
 * borrow this span. */
typedef struct sm64_saturn_geo_walk_runtime_frame {
    uintptr_t node;
    uintptr_t sibling;
    uint16_t phase;
    uint16_t leave_action;
    uint16_t matrix_depth;
    uint16_t context_token;
} sm64_saturn_geo_walk_runtime_frame_t;

extern sm64_saturn_geo_walk_runtime_frame_t
    sourceboot_geo_walk_frames[SM64_SATURN_GEO_TRAVERSAL_CAPACITY];

extern const uint16_t sourceboot_geo_walk_frame_capacity;

#endif
