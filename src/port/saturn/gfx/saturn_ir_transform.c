#include "saturn_ir_transform.h"

#if defined(__sh__)
#include <yaul.h>
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
    const int32_t divisor = view->z < job->near_depth ?
                            job->near_depth : view->z;
    int32_t reciprocal;
    if (!reciprocal_q16(job->focal_length, divisor, &reciprocal)) {
        return false;
    }

    view->x = (int32_t)((((int64_t)relative.x * job->camera.right.x) +
                         ((int64_t)relative.y * job->camera.right.y) +
                         ((int64_t)relative.z * job->camera.right.z)) >> 16);
    view->y = (int32_t)((((int64_t)relative.x * job->camera.up.x) +
                         ((int64_t)relative.y * job->camera.up.y) +
                         ((int64_t)relative.z * job->camera.up.z)) >> 16);
    const int32_t screen_x = job->center_x +
        project_q16(reciprocal, view->x);
    const int32_t screen_y = job->center_y -
        project_q16(reciprocal, view->y);
    projected->x = (int16_t)clamp_coordinate(screen_x, job->coord_min,
                                             job->coord_max);
    projected->y = (int16_t)clamp_coordinate(screen_y, job->coord_min,
                                             job->coord_max);
    projected->z = view->z;
    return true;
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
