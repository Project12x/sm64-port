#include <assert.h>
#include <stdint.h>

#include "saturn_ir_transform.h"

static sm64_saturn_ir_transform_job_t identity_job(void)
{
    return (sm64_saturn_ir_transform_job_t){
        .camera = {
            .position = {0, 0, 0},
            .right = {1 << 16, 0, 0},
            .up = {0, 1 << 16, 0},
            .forward = {0, 0, 1 << 16}
        },
        .focal_length = 256,
        .near_depth = 128,
        .center_x = 160,
        .center_y = 112,
        .coord_min = -1024,
        .coord_max = 1023
    };
}

int main(void)
{
    const sm64_saturn_ir_transform_job_t job = identity_job();
    const sm64_saturn_vec3i_t world[] = {
        {0, 0, 256}, {256, 0, 256}, {0, 256, 256}
    };
    sm64_saturn_vec3i_t view[3];
    sm64_saturn_projected_vertex_t projected[3];
    assert(sm64_saturn_ir_transform_batch(&job, world, view, projected, 3));
    assert(view[0].x == 0 && view[0].y == 0 && view[0].z == 256);
    assert(projected[0].x == 160 && projected[0].y == 112);
    assert(projected[1].x == 416 && projected[1].y == 112);
    assert(projected[2].x == 160 && projected[2].y == -144);
    assert(!sm64_saturn_ir_transform_batch(&job, NULL, view, projected, 1));
    return 0;
}
