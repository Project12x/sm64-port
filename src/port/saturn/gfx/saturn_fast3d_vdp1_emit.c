#include "saturn_fast3d_vdp1_emit.h"

void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;

    sm64_saturn_vdp1_backend_begin(backend);

    /* Far-to-near: iterate buckets from SM64_SATURN_FAST3D_DEPTH_BUCKETS-1
     * down to 0, emitting every resolved triangle whose bucket matches,
     * so nearer geometry draws last (painter's algorithm). O(buckets *
     * triangles) is acceptable at this increment's bounded
     * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES scale. */
    for (int32_t bucket = SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1;
         bucket >= 0; bucket--) {
        for (uint16_t i = 0; i < frontend->resolved_count; i++) {
            const sm64_saturn_resolved_triangle_t *tri =
                &frontend->resolved[i];
            if (tri->depth_bucket != (uint16_t)bucket) {
                continue;
            }

            vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
            if (cmdt == NULL) {
                /* Distinct from the frontend's own reject_command_capacity
                 * (that one is the resolved-triangle buffer, a different
                 * ceiling than this VDP1 command arena -- see
                 * saturn_fast3d_frontend.h's comment on both fields). */
                profile->reject_vdp1_arena_capacity++;
                continue;
            }

            /* (i0, i1, i2, i2) degenerate quad -- last vertex duplicated,
             * matching the convention this port introduces in the
             * frontend's triangle resolve step. */
            const int16_vec2_t quad_vertices[4] = {
                INT16_VEC2_INITIALIZER(tri->x[0], tri->y[0]),
                INT16_VEC2_INITIALIZER(tri->x[1], tri->y[1]),
                INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2]),
                INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2])
            };

            vdp1_cmdt_polygon_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE
            });
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = tri->color_rgb1555
            });
            vdp1_cmdt_vtx_set(cmdt, quad_vertices);
            profile->triangles_vdp1_emitted++;
        }
    }

    sm64_saturn_vdp1_backend_finish(backend);
    sm64_saturn_vdp1_backend_upload(backend);
}
