/* GPL-3.0-only close-port; see ztreme_frustum.h for upstream and changes. */
#include "ztreme_frustum.h"
#include "../gfx/saturn_render_native_math.h"

#include <stdbool.h>
#include <stddef.h>

static int64_t dot_q16(const int32_t basis[3], const int64_t value[3])
{
    return (int64_t)basis[0] * value[0] +
           (int64_t)basis[1] * value[1] +
           (int64_t)basis[2] * value[2];
}

static int64_t support_radius_q16(const int32_t basis[3],
                                  const int64_t extent[3])
{
    int64_t radius_q16 = 0;
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        const int64_t component = basis[axis];
        const int64_t magnitude = component < 0 ? -component : component;
        radius_q16 += magnitude * extent[axis];
    }
    /* Ceil instead of truncating so quantization cannot turn a touching box
     * into a false OUTSIDE result. Z-Treme's original plane-support-point
     * test (ZT_FRUSTUM.c:145-163) is conservative by construction; this
     * center/extent form must preserve the same property. */
    return (radius_q16 + 0xFFFF) >> 16;
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

static int32_t scaled_limit(int64_t depth, int32_t extent, int32_t focal_length)
{
    int32_t result;
    if (!sm64_saturn_div_s64_s32(depth * extent, focal_length, &result)) {
        return INT32_MAX;
    }
    return result;
}

sm64_saturn_ztreme_frustum_result_t sm64_saturn_ztreme_frustum_aabb(
    const sm64_saturn_ztreme_frustum_t *frustum,
    const int32_t minimum[3], const int32_t maximum[3])
{
#if defined(SM64_SATURN_ZTREME_FRUSTUM_REFERENCE)
    if (sm64_saturn_ztreme_frustum_force_reference)
        return sm64_saturn_ztreme_frustum_aabb_reference(frustum, minimum,
                                                         maximum);
#endif
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
    const int64_t z_radius =
        support_radius_q16(frustum->forward, extent_world);
    const int64_t x_radius =
        support_radius_q16(frustum->right, extent_world);
    const int64_t y_radius =
        support_radius_q16(frustum->up, extent_world);
    const int64_t near_limit = frustum->near_depth;
    const int64_t far_limit = frustum->far_depth;
    const int64_t far_z = z + z_radius > near_limit
        ? z + z_radius : near_limit;
    const int64_t near_z = z - z_radius > near_limit
        ? z - z_radius : near_limit;
    const int32_t far_width = scaled_limit(far_z, frustum->half_width,
                                           frustum->focal_length);
    const int32_t far_height = scaled_limit(far_z, frustum->half_height,
                                            frustum->focal_length);
    const int32_t near_width = scaled_limit(near_z, frustum->half_width,
                                            frustum->focal_length);
    const int32_t near_height = scaled_limit(near_z, frustum->half_height,
                                             frustum->focal_length);
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

#if defined(SM64_SATURN_ZTREME_FRUSTUM_REFERENCE)
/* Test-only equivalence-oracle support (Sprint 2 T2.10 item 3,
 * docs/saturn/evidence/reports/sprint2-t2_10-spatial-admit-fixes.md).
 *
 * sm64_saturn_ztreme_frustum_aabb_reference() is a verbatim copy of the
 * pre-T2.10 divided body, pinned here so the cross-multiplied classification
 * is proven against the implementation it replaces rather than against
 * itself. sm64_saturn_ztreme_frustum_force_reference lets the fixture drive
 * the real sm64_saturn_scene_admit() through the pinned body, so the oracle
 * can compare admitted cluster *sets* and not only individual comparisons.
 *
 * SM64_SATURN_ZTREME_FRUSTUM_REFERENCE is defined only by the host test
 * target; no product build defines it, so none of this reaches the image. */
int sm64_saturn_ztreme_frustum_force_reference = 0;

sm64_saturn_ztreme_frustum_result_t sm64_saturn_ztreme_frustum_aabb_reference(
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
        extent_world[axis] = ((int64_t)maximum[axis] - minimum[axis] + 1) / 2;
    }
    int64_t delta[3] = {
        center_world[0] - frustum->position[0],
        center_world[1] - frustum->position[1],
        center_world[2] - frustum->position[2]};
    const int64_t z = dot_q16(frustum->forward, delta) >> 16;
    const int64_t x = dot_q16(frustum->right, delta) >> 16;
    const int64_t y = dot_q16(frustum->up, delta) >> 16;
    const int64_t z_radius =
        support_radius_q16(frustum->forward, extent_world);
    const int64_t x_radius =
        support_radius_q16(frustum->right, extent_world);
    const int64_t y_radius =
        support_radius_q16(frustum->up, extent_world);
    const int64_t near_limit = frustum->near_depth;
    const int64_t far_limit = frustum->far_depth;
    const int64_t far_z = z + z_radius > near_limit
        ? z + z_radius : near_limit;
    const int64_t near_z = z - z_radius > near_limit
        ? z - z_radius : near_limit;
    const int32_t far_width = scaled_limit(far_z, frustum->half_width,
                                           frustum->focal_length);
    const int32_t far_height = scaled_limit(far_z, frustum->half_height,
                                            frustum->focal_length);
    const int32_t near_width = scaled_limit(near_z, frustum->half_width,
                                            frustum->focal_length);
    const int32_t near_height = scaled_limit(near_z, frustum->half_height,
                                             frustum->focal_length);
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
#endif
