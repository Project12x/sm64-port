#ifndef SM64_SATURN_IR_TRANSFORM_H
#define SM64_SATURN_IR_TRANSFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_projected_workarea.h"
#include "saturn_transform.h"

/* A complete, immutable transform snapshot. A batch has no dependency on
 * renderer globals, so the same record can be dispatched to either SH-2. */
typedef struct sm64_saturn_ir_transform_job {
    sm64_saturn_camera_transform_t camera;
    int32_t focal_length;
    int32_t near_depth;
    int16_t center_x;
    int16_t center_y;
    int16_t coord_min;
    int16_t coord_max;
    /* Permit a caller to recover a primitive crossing the near plane by
     * projecting its clipped corner at the plane. Strict rejection remains
     * the default for shared IR callers and host tests. */
    bool clip_near;
} sm64_saturn_ir_transform_job_t;

/* Transform one source-space vertex. `view` and `projected` are disjoint
 * caller-owned outputs; no module state is read or written. */
bool sm64_saturn_ir_transform_one(
    const sm64_saturn_ir_transform_job_t *job,
    sm64_saturn_vec3i_t world,
    sm64_saturn_vec3i_t *view,
    sm64_saturn_projected_vertex_t *projected);

/* Project an already view-space point. Terrain clipping uses this after its
 * fixed-ring near-plane pass so no post-projection interpolation is needed. */
bool sm64_saturn_ir_project_view(
    const sm64_saturn_ir_transform_job_t *job,
    sm64_saturn_vec3i_t view,
    sm64_saturn_projected_vertex_t *projected);

/* Transform a contiguous bank slice. The output arrays must hold `count`
 * entries. This is the job boundary used by the eventual dual-SH2 split. */
bool sm64_saturn_ir_transform_batch(
    const sm64_saturn_ir_transform_job_t *job,
    const sm64_saturn_vec3i_t *world,
    sm64_saturn_vec3i_t *view,
    sm64_saturn_projected_vertex_t *projected,
    uint16_t count);

#endif
