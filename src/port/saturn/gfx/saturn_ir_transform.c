#include "saturn_ir_transform.h"

#if defined(__sh__)
#include <yaul.h>
#endif

/* See the long note at the clamp site: ray-preserving scaling pulls
 * off-screen vertices onto the screen and holes the ground. Kept switchable
 * rather than deleted so the experiment is not re-derived from scratch. */
#ifndef SM64_SATURN_IR_RAY_PRESERVING_CLAMP
#define SM64_SATURN_IR_RAY_PRESERVING_CLAMP 0
#endif

static int32_t clamp_coordinate(int32_t value, int16_t minimum, int16_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static bool reciprocal_q16(int32_t dividend, int32_t divisor, int32_t *result)
{
    if (result == NULL || divisor == 0) return false;
#if defined(__sh__)
    cpu_divu_fix16_set(dividend, divisor);
    *result = (int32_t)cpu_divu_quotient_get();
    return true;
#else
    const int64_t quotient = ((int64_t)dividend << 16) / divisor;
    if (quotient > INT32_MAX || quotient < INT32_MIN) return false;
    *result = (int32_t)quotient;
    return true;
#endif
}

static int32_t project_q16(int32_t reciprocal, int32_t coordinate)
{
#if defined(__sh__)
    return fix16_high_mul((fix16_t)reciprocal, fix16_int32_from(coordinate));
#else
    return (int32_t)(((int64_t)reciprocal * coordinate) >> 16);
#endif
}

bool sm64_saturn_ir_project_view(
    const sm64_saturn_ir_transform_job_t *job,
    sm64_saturn_vec3i_t view,
    sm64_saturn_projected_vertex_t *projected)
{
    if (job == NULL || projected == NULL || job->near_depth <= 0 ||
        job->focal_length <= 0 || job->coord_min > job->coord_max ||
        view.z <= 0) {
        return false;
    }
    int32_t reciprocal;
    if (!reciprocal_q16(job->focal_length, view.z, &reciprocal))
        return false;
    const int32_t screen_x = job->center_x +
        project_q16(reciprocal, view.x);
    const int32_t screen_y = job->center_y -
        project_q16(reciprocal, view.y);
    /* VDP1 coordinates are 11-bit signed, so a vertex projecting outside the
     * box cannot be represented and must be brought back inside. Clamping each
     * axis INDEPENDENTLY -- what this did before -- moves the vertex off its
     * own view ray: (5000, 3000) becomes (1023, 1023), a completely different
     * direction from centre. The vertex was off-screen either way, so the
     * visible defect is not the vertex, it is the EDGE joining it to an
     * on-screen vertex, which now runs at the wrong angle. That wrong slope is
     * the long-standing terrain texture warp, worst near the bottom of the
     * screen where the ground plane projects furthest out of range.
     *
     * Scaling both axes by one common factor keeps the vertex on the same ray
     * from centre, so every edge through the visible region keeps its true
     * direction. This is not a substitute for real side-plane clipping -- the
     * exact fix inserts the boundary intersection and can add vertices, which
     * VDP1's 4-corner commands cannot carry without splitting -- but it costs
     * no extra commands, no texture bytes, and two comparisons per vertex on
     * the common in-range path. The castle demo avoids the whole problem by
     * subdividing at bake time; BOB cannot, because its texture bank is at
     * exactly 100% (333,696 of 333,696 bytes). */
    /* RAY-PRESERVING SCALING IS OFF. It fixed edge direction but introduced a
     * worse defect, owner-observed as a band of background showing through the
     * lower screen.
     *
     * The common scale factor is taken from whichever axis is most out of
     * range, and it is then applied to BOTH axes. A vertex far to the side but
     * only slightly below the screen therefore gets pulled UP onto it: with
     * centre (160, 112), a vertex at (10000, 300) has extent_x 9840 against
     * limit_x 863 -- scale 0.0877 -- while its y needed no correction at all.
     * y becomes 112 + 188 * 0.0877 = 128, so a corner that sat safely below the
     * 224-pixel screen lands in the middle of it, the quad shrinks upward, and
     * the ground develops a hole.
     *
     * Per-axis clamping is wrong about edge direction but never moves a vertex
     * from off-screen to on-screen, so it fails visibly smaller. The two goals
     * -- preserve the ray, and keep out-of-range vertices out of view -- cannot
     * both be met by moving a vertex; that is what subdivision is for. */
#if SM64_SATURN_IR_RAY_PRESERVING_CLAMP
    int32_t offset_x = screen_x - job->center_x;
    int32_t offset_y = screen_y - job->center_y;
    const int32_t limit_x = offset_x >= 0
        ? (int32_t)job->coord_max - job->center_x
        : job->center_x - (int32_t)job->coord_min;
    const int32_t limit_y = offset_y >= 0
        ? (int32_t)job->coord_max - job->center_y
        : job->center_y - (int32_t)job->coord_min;
    const int32_t extent_x = offset_x >= 0 ? offset_x : -offset_x;
    const int32_t extent_y = offset_y >= 0 ? offset_y : -offset_y;
    if ((extent_x > limit_x && limit_x >= 0) ||
        (extent_y > limit_y && limit_y >= 0)) {
        /* Q16 ratio; limit is bounded by the coordinate box so limit << 16
         * stays inside int32 and this is a 32-bit divide, not a 64-bit one. */
        int32_t scale_q16 = 1 << 16;
        if (extent_x > limit_x && extent_x > 0) {
            const int32_t candidate = (limit_x << 16) / extent_x;
            if (candidate < scale_q16) scale_q16 = candidate;
        }
        if (extent_y > limit_y && extent_y > 0) {
            const int32_t candidate = (limit_y << 16) / extent_y;
            if (candidate < scale_q16) scale_q16 = candidate;
        }
        if (scale_q16 < 0) scale_q16 = 0;
        offset_x = (int32_t)(((int64_t)offset_x * scale_q16) >> 16);
        offset_y = (int32_t)(((int64_t)offset_y * scale_q16) >> 16);
    }
    projected->x = (int16_t)clamp_coordinate(job->center_x + offset_x,
                                             job->coord_min, job->coord_max);
    projected->y = (int16_t)clamp_coordinate(job->center_y + offset_y,
                                             job->coord_min, job->coord_max);
#else
    projected->x = (int16_t)clamp_coordinate(screen_x, job->coord_min,
                                             job->coord_max);
    projected->y = (int16_t)clamp_coordinate(screen_y, job->coord_min,
                                             job->coord_max);
#endif
    projected->z = view.z;
    return true;
}

bool sm64_saturn_ir_transform_one(
    const sm64_saturn_ir_transform_job_t *job,
    sm64_saturn_vec3i_t world,
    sm64_saturn_vec3i_t *view,
    sm64_saturn_projected_vertex_t *projected)
{
    if (job == NULL || view == NULL || projected == NULL ||
        job->near_depth <= 0 || job->focal_length <= 0 ||
        job->coord_min > job->coord_max) {
        return false;
    }

    const sm64_saturn_vec3i_t relative = {
        world.x - job->camera.position.x,
        world.y - job->camera.position.y,
        world.z - job->camera.position.z
    };
    view->z = (int32_t)((((int64_t)relative.x * job->camera.forward.x) +
                         ((int64_t)relative.y * job->camera.forward.y) +
                         ((int64_t)relative.z * job->camera.forward.z)) >> 16);
    const bool near_clipped = view->z <= job->near_depth;
    if (near_clipped && !job->clip_near) return false;
    view->x = (int32_t)((((int64_t)relative.x * job->camera.right.x) +
                         ((int64_t)relative.y * job->camera.right.y) +
                         ((int64_t)relative.z * job->camera.right.z)) >> 16);
    view->y = (int32_t)((((int64_t)relative.x * job->camera.up.x) +
                         ((int64_t)relative.y * job->camera.up.y) +
                         ((int64_t)relative.z * job->camera.up.z)) >> 16);
    sm64_saturn_vec3i_t projected_view = *view;
    if (near_clipped) projected_view.z = job->near_depth;
    return sm64_saturn_ir_project_view(job, projected_view, projected);
}

bool sm64_saturn_ir_transform_batch(
    const sm64_saturn_ir_transform_job_t *job,
    const sm64_saturn_vec3i_t *world,
    sm64_saturn_vec3i_t *view,
    sm64_saturn_projected_vertex_t *projected,
    uint16_t count)
{
    if (count != 0 && (world == NULL || view == NULL || projected == NULL)) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (!sm64_saturn_ir_transform_one(job, world[index], &view[index],
                                           &projected[index])) {
            return false;
        }
    }
    return true;
}
