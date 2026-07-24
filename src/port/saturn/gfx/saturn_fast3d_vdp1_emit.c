#include "saturn_fast3d_vdp1_emit.h"

#include "saturn_gouraud.h"
#include "saturn_gouraud_bank.h"
#include "../gpl/slavedriver_dma_queue.h" /* gpl/ is a sibling of gfx/ under
                                           * src/port/saturn/; no -I path
                                           * exposes gpl/ by bare name, so
                                           * this matches hwtest's existing
                                           * "../gpl/..." include style
                                           * rather than a bare filename. */

_Static_assert(sizeof(sm64_saturn_gouraud_table_t) ==
               sizeof(vdp1_gouraud_table_t),
               "staging table must match Yaul's VDP1 layout");

void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend,
                                  sm64_saturn_gouraud_bank_t *gouraud_bank)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;

    sm64_saturn_gouraud_bank_begin(gouraud_bank);
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

            uintptr_t grda_addr;
            sm64_saturn_gouraud_table_t *table =
                sm64_saturn_gouraud_bank_alloc(gouraud_bank, &grda_addr);

            vdp1_cmdt_polygon_set(cmdt);
            if (table != NULL) {
                /* Table entries ARE the final corner colors: with the
                 * neutral base (0xC210, R=G=B=16) VDP1's signed
                 * correction makes 16 + (entry - 16) == entry
                 * (saturn_gouraud.h). Corner D duplicates C, matching
                 * the (i0,i1,i2,i2) degenerate-quad convention. */
                table->colors[0] = tri->corner_rgb1555[0];
                table->colors[1] = tri->corner_rgb1555[1];
                table->colors[2] = tri->corner_rgb1555[2];
                table->colors[3] = tri->corner_rgb1555[2];
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_GOURAUD
                });
                vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                    .raw = sm64_saturn_gouraud_neutral_color()
                });
                vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)grda_addr);
            } else {
                /* Bank exhausted or partition unusable: counted flat
                 * fallback (degradation contract). */
                profile->gouraud_bank_overflow++;
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_REPLACE
                });
                vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                    .raw = tri->corner_rgb1555[0]
                });
            }
            vdp1_cmdt_vtx_set(cmdt, quad_vertices);
            profile->triangles_vdp1_emitted++;
        }
    }

    if (sm64_saturn_gouraud_bank_used_bytes(gouraud_bank) > 0) {
        /* Used-prefix only (the E0 command-arena lesson): synchronous
         * for bring-up, same as the backend's own upload. Tables must
         * be resident in VDP1 VRAM before backend_upload() lets VDP1
         * start plotting commands that reference them via CMDGRDA. */
        saturn_dma_queue_transfer_wait(
            (void *)gouraud_bank->vram_base, gouraud_bank->staging,
            sm64_saturn_gouraud_bank_used_bytes(gouraud_bank),
            SATURN_DMA_QUEUE_SCU);
    }

    sm64_saturn_vdp1_backend_finish(backend);
    sm64_saturn_vdp1_backend_upload(backend);
}
