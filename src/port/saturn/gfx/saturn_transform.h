#ifndef SM64_SATURN_TRANSFORM_H
#define SM64_SATURN_TRANSFORM_H

#include <stdint.h>

/* Scene-neutral Q16 camera/transform records shared by harnesses and the
 * eventual Fast3D frame front end. Positions remain in source world units;
 * basis vectors are signed Q16.16 values. */
typedef struct sm64_saturn_vec3i {
    int32_t x;
    int32_t y;
    int32_t z;
} sm64_saturn_vec3i_t;

typedef struct sm64_saturn_camera_transform {
    sm64_saturn_vec3i_t position;
    sm64_saturn_vec3i_t right;
    sm64_saturn_vec3i_t up;
    sm64_saturn_vec3i_t forward;
} sm64_saturn_camera_transform_t;

static inline uint32_t
sm64_saturn_isqrt_u64(uint64_t value)
{
    uint64_t root = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

static inline sm64_saturn_vec3i_t
sm64_saturn_vec3_normalize_q16(sm64_saturn_vec3i_t value)
{
    const uint32_t length = sm64_saturn_isqrt_u64(
        (uint64_t)((int64_t)value.x * value.x) +
        (uint64_t)((int64_t)value.y * value.y) +
        (uint64_t)((int64_t)value.z * value.z));

    if (length == 0) {
        return (sm64_saturn_vec3i_t){0, 0, 0};
    }
    return (sm64_saturn_vec3i_t){
        (int32_t)(((int64_t)value.x << 16) / length),
        (int32_t)(((int64_t)value.y << 16) / length),
        (int32_t)(((int64_t)value.z << 16) / length)
    };
}

static inline sm64_saturn_vec3i_t
sm64_saturn_world_to_view(const sm64_saturn_camera_transform_t *camera,
                          sm64_saturn_vec3i_t world)
{
    const sm64_saturn_vec3i_t relative = {
        world.x - camera->position.x,
        world.y - camera->position.y,
        world.z - camera->position.z
    };

    return (sm64_saturn_vec3i_t){
        (int32_t)((((int64_t)relative.x * camera->right.x) +
                   ((int64_t)relative.y * camera->right.y) +
                   ((int64_t)relative.z * camera->right.z)) >> 16),
        (int32_t)((((int64_t)relative.x * camera->up.x) +
                   ((int64_t)relative.y * camera->up.y) +
                   ((int64_t)relative.z * camera->up.z)) >> 16),
        (int32_t)((((int64_t)relative.x * camera->forward.x) +
                   ((int64_t)relative.y * camera->forward.y) +
                   ((int64_t)relative.z * camera->forward.z)) >> 16)
    };
}

#endif
