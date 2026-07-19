/*
 * SPDX-License-Identifier: MIT
 *
 * Work-area layout and clip-mask reduction adapted for this project from
 * libyaul/libmic3d render.c at commit
 * 6012f79f237773378c8014e70d8998ad95a38d98.
 * Copyright (c) 2012-2023 Israel Jacquez <mrkotfw@gmail.com>
 * See third_party/libyaul/LICENSE for the MIT license text.
 */

#ifndef SM64_SATURN_PROJECTED_WORKAREA_H
#define SM64_SATURN_PROJECTED_WORKAREA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_projected_vertex {
    int16_t x;
    int16_t y;
    int32_t z;
} sm64_saturn_projected_vertex_t;

/* Caller-owned, bounded storage for transform-once projected vertices. */
typedef struct sm64_saturn_projected_workarea {
    sm64_saturn_projected_vertex_t *vertices;
    uint16_t capacity;
    uint16_t count;
    uint16_t peak;
    bool overflowed;
} sm64_saturn_projected_workarea_t;

typedef struct sm64_saturn_viewport {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
} sm64_saturn_viewport_t;

typedef enum sm64_saturn_clip_flags {
    SM64_SATURN_CLIP_NONE = 0,
    SM64_SATURN_CLIP_LEFT = 1U << 0,
    SM64_SATURN_CLIP_RIGHT = 1U << 1,
    SM64_SATURN_CLIP_TOP = 1U << 2,
    SM64_SATURN_CLIP_BOTTOM = 1U << 3
} sm64_saturn_clip_flags_t;

typedef struct sm64_saturn_projected_quad {
    int16_t min_x;
    int16_t min_y;
    int16_t max_x;
    int16_t max_y;
    int32_t min_z;
    int32_t max_z;
    int32_t center_z;
    uint8_t clip_and;
    uint8_t clip_or;
} sm64_saturn_projected_quad_t;

static inline void
sm64_saturn_projected_workarea_init(
    sm64_saturn_projected_workarea_t *workarea,
    sm64_saturn_projected_vertex_t *vertices, uint16_t capacity)
{
    workarea->vertices = vertices;
    workarea->capacity = capacity;
    workarea->count = 0;
    workarea->peak = 0;
    workarea->overflowed = vertices == NULL && capacity != 0;
}

static inline void
sm64_saturn_projected_workarea_reset(
    sm64_saturn_projected_workarea_t *workarea)
{
    workarea->count = 0;
    workarea->overflowed = workarea->vertices == NULL &&
                           workarea->capacity != 0;
}

/* Append one already-transformed/projected vertex. Projection policy and any
 * SH2 DIVU scheduling remain outside this portable cache contract. */
static inline bool
sm64_saturn_projected_workarea_push(
    sm64_saturn_projected_workarea_t *workarea,
    sm64_saturn_projected_vertex_t vertex, uint16_t *index)
{
    if (workarea->vertices == NULL || workarea->count >= workarea->capacity) {
        workarea->overflowed = true;
        return false;
    }

    const uint16_t slot = workarea->count;
    workarea->vertices[slot] = vertex;
    workarea->count++;
    if (workarea->count > workarea->peak) {
        workarea->peak = workarea->count;
    }
    if (index != NULL) {
        *index = slot;
    }
    return true;
}

static inline uint8_t
sm64_saturn_projected_vertex_clip_flags(
    sm64_saturn_projected_vertex_t vertex,
    const sm64_saturn_viewport_t *viewport)
{
    uint8_t flags = SM64_SATURN_CLIP_NONE;

    if (vertex.x < viewport->left) {
        flags |= SM64_SATURN_CLIP_LEFT;
    }
    if (vertex.x > viewport->right) {
        flags |= SM64_SATURN_CLIP_RIGHT;
    }
    if (vertex.y < viewport->top) {
        flags |= SM64_SATURN_CLIP_TOP;
    }
    if (vertex.y > viewport->bottom) {
        flags |= SM64_SATURN_CLIP_BOTTOM;
    }
    return flags;
}

/* Gather a four-corner polygon entirely from cached projected vertices. The
 * function performs no transform or perspective division. */
static inline bool
sm64_saturn_projected_quad_analyze(
    const sm64_saturn_projected_workarea_t *workarea,
    const uint16_t indices[4], const sm64_saturn_viewport_t *viewport,
    sm64_saturn_projected_quad_t *quad)
{
    if (workarea == NULL || workarea->vertices == NULL || indices == NULL ||
        viewport == NULL || quad == NULL) {
        return false;
    }
    for (uint8_t corner = 0; corner < 4; corner++) {
        if (indices[corner] >= workarea->count) {
            return false;
        }
    }

    const sm64_saturn_projected_vertex_t first =
        workarea->vertices[indices[0]];
    quad->min_x = first.x;
    quad->max_x = first.x;
    quad->min_y = first.y;
    quad->max_y = first.y;
    quad->min_z = first.z;
    quad->max_z = first.z;
    quad->clip_and = UINT8_MAX;
    quad->clip_or = SM64_SATURN_CLIP_NONE;

    for (uint8_t corner = 0; corner < 4; corner++) {
        const sm64_saturn_projected_vertex_t vertex =
            workarea->vertices[indices[corner]];
        const uint8_t flags =
            sm64_saturn_projected_vertex_clip_flags(vertex, viewport);

        if (vertex.x < quad->min_x) quad->min_x = vertex.x;
        if (vertex.x > quad->max_x) quad->max_x = vertex.x;
        if (vertex.y < quad->min_y) quad->min_y = vertex.y;
        if (vertex.y > quad->max_y) quad->max_y = vertex.y;
        if (vertex.z < quad->min_z) quad->min_z = vertex.z;
        if (vertex.z > quad->max_z) quad->max_z = vertex.z;
        quad->clip_and &= flags;
        quad->clip_or |= flags;
    }
    quad->center_z = (int32_t)(((int64_t)workarea->vertices[indices[0]].z +
                                workarea->vertices[indices[2]].z) / 2);
    return true;
}

/* VDP1 has no homogeneous polygon clipper. Until source polygons are split,
 * reject a quad that crosses either depth plane. XY intersection is the
 * libmic3d-style AND reduction of per-corner clip flags. */
static inline bool
sm64_saturn_projected_quad_is_visible(
    const sm64_saturn_projected_quad_t *quad,
    int32_t near_depth, int32_t far_depth, int16_t maximum_span)
{
    if (quad == NULL || near_depth > far_depth) {
        return false;
    }
    if (quad->min_z < near_depth || quad->max_z > far_depth ||
        quad->clip_and != SM64_SATURN_CLIP_NONE) {
        return false;
    }
    if (maximum_span > 0 &&
        ((int32_t)quad->max_x - quad->min_x > maximum_span ||
         (int32_t)quad->max_y - quad->min_y > maximum_span)) {
        return false;
    }
    return true;
}

#endif
