/* GPL-3.0-only close-port; see ztreme_frustum.h for upstream and changes. */
#include "ztreme_frustum.h"

#include <stdbool.h>
#include <stddef.h>

static int64_t dot_q16(const int32_t basis[3], const int64_t value[3])
{
    return (int64_t)basis[0] * value[0] +
           (int64_t)basis[1] * value[1] +
           (int64_t)basis[2] * value[2];
}

static bool interval_outside(int64_t center, int64_t radius,
                             int64_t minimum, int64_t maximum)
{
    return center + radius < minimum || center - radius > maximum;
}

static bool interval_inside(int64_t center, int64_t radius,
                            int64_t minimum, int64_t maximum)
{
    return center - radius >= minimum && center + radius <= maximum;
}

sm64_saturn_ztreme_frustum_result_t sm64_saturn_ztreme_frustum_aabb(
    const sm64_saturn_ztreme_frustum_t *frustum,
    const int32_t minimum[3], const int32_t maximum[3])
{
    if (frustum == NULL || minimum == NULL || maximum == NULL) {
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    }
    int64_t center_world[3];
    int64_t extent_world[3];
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        center_world[axis] = ((int64_t)minimum[axis] + maximum[axis]) / 2;
        /* Ceil the half-extent so integer quantization never turns a tight
         * bound into an under-approximation at an odd-sized edge. */
        extent_world[axis] = ((int64_t)maximum[axis] - minimum[axis] + 1) / 2;
    }
    int64_t delta[3] = {
        center_world[0] - frustum->position[0],
        center_world[1] - frustum->position[1],
        center_world[2] - frustum->position[2]};
    const int64_t z = dot_q16(frustum->forward, delta) >> 16;
    const int64_t x = dot_q16(frustum->right, delta) >> 16;
    const int64_t y = dot_q16(frustum->up, delta) >> 16;
    const int64_t z_radius = (dot_q16(frustum->forward, extent_world) < 0
        ? -dot_q16(frustum->forward, extent_world)
        : dot_q16(frustum->forward, extent_world)) >> 16;
    const int64_t x_radius = (dot_q16(frustum->right, extent_world) < 0
        ? -dot_q16(frustum->right, extent_world)
        : dot_q16(frustum->right, extent_world)) >> 16;
    const int64_t y_radius = (dot_q16(frustum->up, extent_world) < 0
        ? -dot_q16(frustum->up, extent_world)
        : dot_q16(frustum->up, extent_world)) >> 16;
    const int64_t near_limit = frustum->near_depth;
    const int64_t far_limit = frustum->far_depth;
    const int64_t far_z = z + z_radius > near_limit
        ? z + z_radius : near_limit;
    const int64_t near_z = z - z_radius > near_limit
        ? z - z_radius : near_limit;
    const int64_t far_width = far_z * frustum->half_width /
                              frustum->focal_length;
    const int64_t far_height = far_z * frustum->half_height /
                               frustum->focal_length;
    const int64_t near_width = near_z * frustum->half_width /
                               frustum->focal_length;
    const int64_t near_height = near_z * frustum->half_height /
                                frustum->focal_length;
    if (interval_outside(z, z_radius, near_limit, far_limit) ||
        interval_outside(x, x_radius, -far_width, far_width) ||
        interval_outside(y, y_radius, -far_height, far_height)) {
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    }
    if (interval_inside(z, z_radius, near_limit, far_limit) &&
        interval_inside(x, x_radius, -near_width, near_width) &&
        interval_inside(y, y_radius, -near_height, near_height)) {
        return SM64_SATURN_ZTREME_FRUSTUM_INSIDE;
    }
    return SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS;
}
