#include <assert.h>
#include <stdint.h>

#include "ztreme_frustum.h"

int main(void)
{
    const sm64_saturn_ztreme_frustum_t frustum = {
        .position = {0, 0, 0},
        .right = {46341, 0, -46341},
        .up = {0, 65536, 0},
        .forward = {46341, 0, 46341},
        .near_depth = 1,
        .far_depth = 1000,
        .half_width = 160,
        .half_height = 112,
        .focal_length = 160};

    /* This box intersects the right edge. abs(dot(axis, extents)) wrongly
     * cancels its +X/-Z contributions and reports OUTSIDE. */
    const int32_t crossing_minimum[3] = {324, -1, -241};
    const int32_t crossing_maximum[3] = {524, 1, -41};
    assert(sm64_saturn_ztreme_frustum_aabb(
               &frustum, crossing_minimum, crossing_maximum) ==
           SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS);

    const int32_t outside_minimum[3] = {2000, -1, -2000};
    const int32_t outside_maximum[3] = {2100, 1, -1900};
    assert(sm64_saturn_ztreme_frustum_aabb(
               &frustum, outside_minimum, outside_maximum) ==
           SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE);
    return 0;
}
