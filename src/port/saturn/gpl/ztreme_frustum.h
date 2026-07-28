/*
 * GPL-3.0-only. Close-port of the tri-state AABB/frustum decision used by
 * SONIC Z-TREME's ZT_FRUSTUM.c:126-161 at cff75451c1616aac1236fc2b44223902b55c706b.
 * The plane math is adapted to the SM64 Saturn camera basis and fixed-point
 * world coordinates; no Z-Treme map/PVS or asset data is copied.
 */
#ifndef SM64_SATURN_ZTREME_FRUSTUM_H
#define SM64_SATURN_ZTREME_FRUSTUM_H

#include <stdint.h>

typedef enum sm64_saturn_ztreme_frustum_result {
    SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE = 0,
    SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS = 1,
    SM64_SATURN_ZTREME_FRUSTUM_INSIDE = 2
} sm64_saturn_ztreme_frustum_result_t;

typedef struct sm64_saturn_ztreme_frustum {
    int32_t position[3];
    int32_t right[3];
    int32_t up[3];
    int32_t forward[3];
    int32_t near_depth;
    int32_t far_depth;
    int32_t half_width;
    int32_t half_height;
    int32_t focal_length;
} sm64_saturn_ztreme_frustum_t;

sm64_saturn_ztreme_frustum_result_t sm64_saturn_ztreme_frustum_aabb(
    const sm64_saturn_ztreme_frustum_t *frustum,
    const int32_t minimum[3], const int32_t maximum[3]);

#endif
