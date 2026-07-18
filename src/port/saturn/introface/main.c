/*
 * First source-derived SM64 geometry on Saturn.
 *
 * mario_face_mesh.h is a direct conversion of the Goddard face vertices and
 * triangles in src/goddard/dynlists/dynlist_mario_face.c.  This renderer uses
 * a temporary, documented material palette rather than original textures.
 */
#include <yaul.h>
#include <string.h>

#include "mario_face_mesh.h"

#define COMMAND_COUNT (SM64_FACE_TRIANGLE_COUNT + 3U)

static vdp1_gouraud_table_t gouraud[SM64_FACE_TRIANGLE_COUNT];
static uint16_t draw_order[SM64_FACE_TRIANGLE_COUNT];

static rgb1555_t
material_color(uint16_t material, int shade)
{
    const uint8_t *rgb = sm64_face_material_rgb[material & 7U];
    /* The original source supplies matching ambient/diffuse colours.  Keep
     * them exact at the bright endpoint and use a half-intensity Gouraud
     * endpoint for the first Saturn lighting pass. */
    const uint8_t divisor = shade > 0 ? 1U : 2U;
    return RGB1555(1, rgb[0] / divisor, rgb[1] / divisor, rgb[2] / divisor);
}

static int16_vec2_t
project_vertex(uint16_t index)
{
    const int16_t *v = sm64_face_vertices[index];
    /* Goddard face coordinates: X is horizontal and Y is vertical. */
    return (int16_vec2_t)INT16_VEC2_INITIALIZER(160 + (v[0] / 6), 160 - (v[1] / 6));
}

static int
depth_of(uint16_t triangle)
{
    const uint16_t *f = sm64_face_triangles[triangle];
    return sm64_face_vertices[f[1]][2] + sm64_face_vertices[f[2]][2] + sm64_face_vertices[f[3]][2];
}

static void
sort_for_painter(void)
{
    for (uint16_t i = 0; i < SM64_FACE_TRIANGLE_COUNT; i++)
        draw_order[i] = i;
    /* Stable insertion sort: far Z first, nearer surface last. */
    for (uint16_t i = 1; i < SM64_FACE_TRIANGLE_COUNT; i++) {
        const uint16_t chosen = draw_order[i];
        const int depth = depth_of(chosen);
        uint16_t j = i;
        while (j > 0 && depth_of(draw_order[j - 1]) > depth) {
            draw_order[j] = draw_order[j - 1];
            j--;
        }
        draw_order[j] = chosen;
    }
}

static void
draw_source_face(void)
{
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
    const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
    vdp1_vram_partitions_t partitions;
    vdp1_cmdt_list_t *list = vdp1_cmdt_list_alloc(COMMAND_COUNT);
    if (list == NULL)
        return;
    sort_for_painter();
    list->count = COMMAND_COUNT;
    (void)memset(list->cmdts, 0, sizeof(vdp1_cmdt_t) * list->count);
    vdp1_cmdt_system_clip_coord_set(&list->cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&list->cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&list->cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&list->cmdts[1], local);

    const vdp1_cmdt_draw_mode_t mode = {
        .color_mode = VDP1_CMDT_CM_RGB_32768,
        .cc_mode = VDP1_CMDT_CC_GOURAUD,
    };
    for (uint16_t out = 0; out < SM64_FACE_TRIANGLE_COUNT; out++) {
        const uint16_t source = draw_order[out];
        const uint16_t *f = sm64_face_triangles[source];
        const int16_t *a = sm64_face_vertices[f[1]];
        const int16_t *b = sm64_face_vertices[f[2]];
        const int16_t *c = sm64_face_vertices[f[3]];
        const int16_vec2_t vertices[4] = {
            project_vertex(f[1]), project_vertex(f[2]), project_vertex(f[3]), project_vertex(f[3])
        };
        vdp1_gouraud_table_t *shade = &gouraud[out];
        shade->colors[0] = material_color(f[0], a[0] + a[1] - a[2] > 0);
        shade->colors[1] = material_color(f[0], b[0] + b[1] - b[2] > 0);
        shade->colors[2] = material_color(f[0], c[0] + c[1] - c[2] > 0);
        shade->colors[3] = shade->colors[2];
        vdp1_cmdt_t *cmdt = &list->cmdts[out + 2U];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, material_color(f[0], 1));
        vdp1_cmdt_vtx_set(cmdt, vertices);
    }
    vdp1_cmdt_end_set(&list->cmdts[SM64_FACE_TRIANGLE_COUNT + 2U]);
    vdp1_vram_partitions_get(&partitions);
    scu_dma_transfer(0, (void *)partitions.gouraud_base, gouraud, sizeof(gouraud));
    scu_dma_transfer_wait(0);
    for (uint16_t i = 0; i < SM64_FACE_TRIANGLE_COUNT; i++)
        vdp1_cmdt_gouraud_base_set(&list->cmdts[i + 2U], (vdp1_vram_t)partitions.gouraud_base + (i * sizeof(vdp1_gouraud_table_t)));
    vdp1_sync_cmdt_list_put(list, 0);
    vdp1_sync_render();
    vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
    vdp1_cmdt_list_free(list);
}

void
user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 0, 0, 5));
    vdp1_env_t env;
    vdp1_env_default_init(&env);
    env.erase_color = RGB1555(1, 0, 0, 5);
    vdp1_env_set(&env);
    for (uint8_t i = 0; i < 8; i++) vdp2_sprite_priority_set(i, 7);
    vdp2_tvmd_display_set();
    dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load();
    dbgio_puts("SM64 SATURN\nSOURCE FACE\n440 VERTICES / 877 TRIS\nGOURAUD VDP1");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    draw_source_face();
    for (;;) {}
}

int main(void) { user_init(); return 0; }
