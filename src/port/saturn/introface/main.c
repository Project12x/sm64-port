/*
 * First source-derived SM64 geometry on Saturn.
 *
 * mario_face_mesh.h is a direct conversion of the Goddard face plus eye
 * vertices and triangles. The eyes include the original iris, pupil, and
 * highlight surfaces from dynlists_mario_eyes.c.
 */
#include <yaul.h>
#include <string.h>

#include "mario_face_mesh.h"

#define EYE_TRIANGLE_COUNT (SM64_RIGHT_EYE_TRIANGLE_COUNT + SM64_LEFT_EYE_TRIANGLE_COUNT)
#define COMMAND_COUNT (SM64_FACE_TRIANGLE_COUNT + EYE_TRIANGLE_COUNT + 3U)

static vdp1_gouraud_table_t gouraud[SM64_FACE_TRIANGLE_COUNT];
static uint16_t draw_order[SM64_FACE_TRIANGLE_COUNT];
static int32_t vertex_normals[SM64_FACE_VERTEX_COUNT][3];

static rgb1555_t
material_color(uint16_t material, uint8_t intensity)
{
    const uint8_t *rgb = sm64_face_material_rgb[material & 7U];
    return RGB1555(1, (rgb[0] * intensity) / 31U,
      (rgb[1] * intensity) / 31U, (rgb[2] * intensity) / 31U);
}

static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }

static void
build_vertex_normals(void)
{
    (void)memset(vertex_normals, 0, sizeof(vertex_normals));
    for (uint16_t i = 0; i < SM64_FACE_TRIANGLE_COUNT; i++) {
        const uint16_t *f = sm64_face_triangles[i];
        const int16_t *a = sm64_face_vertices[f[1]];
        const int16_t *b = sm64_face_vertices[f[2]];
        const int16_t *c = sm64_face_vertices[f[3]];
        const int32_t abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
        const int32_t acx = c[0] - a[0], acy = c[1] - a[1], acz = c[2] - a[2];
        const int32_t nx = ((aby * acz) - (abz * acy)) / 128;
        const int32_t ny = ((abz * acx) - (abx * acz)) / 128;
        const int32_t nz = ((abx * acy) - (aby * acx)) / 128;
        for (uint8_t corner = 1; corner < 4; corner++) {
            int32_t *normal = vertex_normals[f[corner]];
            normal[0] += nx; normal[1] += ny; normal[2] += nz;
        }
    }
}

static uint8_t
vertex_intensity(uint16_t index)
{
    const int32_t *normal = vertex_normals[index];
    /* Camera-facing, upper-left key light; abs supports the source's mixed
     * winding while preserving the surface-angle contrast for this study. */
    const int32_t dot = abs32((-3 * normal[0]) + (4 * normal[1]) + (6 * normal[2]));
    const int32_t maximum = 6 * (abs32(normal[0]) + abs32(normal[1]) + abs32(normal[2]));
    return maximum == 0 ? 18U : (uint8_t)(6 + ((dot * 25) / maximum));
}

static int16_vec2_t
project_point(const int16_t *v)
{
    /* Goddard face coordinates: X is horizontal and Y is vertical. */
    return (int16_vec2_t)INT16_VEC2_INITIALIZER(160 + (v[0] / 6), 160 - (v[1] / 6));
}

static int16_vec2_t
project_eye_point(const int16_t *v, int16_t offset_x, int16_t offset_y,
  int16_t center_x, int16_t center_y)
{
    int16_vec2_t point = project_point(v);
    /* The raw eye objects are too large against the source face's eye-surface
     * geometry at this camera. Reduce around the calibrated surface center. */
    point.x = center_x + (((point.x + offset_x - center_x) * 2) / 3);
    point.y = center_y + (((point.y + offset_y - center_y) * 2) / 3);
    return point;
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
draw_eye(vdp1_cmdt_t *cmdts, uint16_t *cursor, const int16_t vertices[][3],
  const uint16_t triangles[][4], uint16_t triangle_count,
  const uint8_t materials[][3], int16_t offset_x, int16_t offset_y,
  int16_t center_x, int16_t center_y)
{
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768 };
    for (uint16_t i = 0; i < triangle_count; i++) {
        const uint16_t *f = triangles[i];
        const uint8_t *rgb = materials[f[0] & 3U];
        const int16_vec2_t projected[4] = {
            project_eye_point(vertices[f[1]], offset_x, offset_y, center_x, center_y), project_eye_point(vertices[f[2]], offset_x, offset_y, center_x, center_y),
            project_eye_point(vertices[f[3]], offset_x, offset_y, center_x, center_y), project_eye_point(vertices[f[3]], offset_x, offset_y, center_x, center_y)
        };
        vdp1_cmdt_t *cmdt = &cmdts[*cursor];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
        vdp1_cmdt_vtx_set(cmdt, projected);
        (*cursor)++;
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
    build_vertex_normals();
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
            project_point(a), project_point(b), project_point(c), project_point(c)
        };
        vdp1_gouraud_table_t *shade = &gouraud[out];
        shade->colors[0] = material_color(f[0], vertex_intensity(f[1]));
        shade->colors[1] = material_color(f[0], vertex_intensity(f[2]));
        shade->colors[2] = material_color(f[0], vertex_intensity(f[3]));
        shade->colors[3] = shade->colors[2];
        vdp1_cmdt_t *cmdt = &list->cmdts[out + 2U];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, material_color(f[0], 31));
        vdp1_cmdt_vtx_set(cmdt, vertices);
    }
    uint16_t cursor = SM64_FACE_TRIANGLE_COUNT + 2U;
    /* Eye surfaces are separate original objects. Scale 2/3 about each source
     * eye-surface centre, after right (+5,-4) / left (-6,-4) calibration. */
    draw_eye(list->cmdts, &cursor, sm64_right_eye_vertices, sm64_right_eye_triangles,
      SM64_RIGHT_EYE_TRIANGLE_COUNT, sm64_right_eye_material_rgb, 5, -4, 179, 128);
    draw_eye(list->cmdts, &cursor, sm64_left_eye_vertices, sm64_left_eye_triangles,
      SM64_LEFT_EYE_TRIANGLE_COUNT, sm64_left_eye_material_rgb, -6, -4, 139, 128);
    vdp1_cmdt_end_set(&list->cmdts[cursor]);
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
    dbgio_puts("SM64 SATURN\nSOURCE FACE + EYES\n440 + 96 VERTICES\n1041 TRIANGLES");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    draw_source_face();
    for (;;) {}
}

int main(void) { user_init(); return 0; }
