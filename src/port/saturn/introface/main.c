/*
 * First source-derived SM64 geometry on Saturn.
 *
 * mario_face_mesh.h is a direct conversion of the Goddard face, eyes,
 * eyebrows, and moustache vertices and triangles.
 */
#include <yaul.h>
#include <string.h>

#include "mario_face_mesh.h"

#define EYE_TRIANGLE_COUNT (SM64_RIGHT_EYE_TRIANGLE_COUNT + SM64_LEFT_EYE_TRIANGLE_COUNT)
#define FEATURE_TRIANGLE_COUNT (SM64_RIGHT_EYEBROW_TRIANGLE_COUNT + SM64_LEFT_EYEBROW_TRIANGLE_COUNT + SM64_MUSTACHE_TRIANGLE_COUNT)
#define FACE_SURFACE_COUNT SM64_FACE_PRIMITIVE_COUNT
#define RIGHT_EYE_SURFACE_BASE FACE_SURFACE_COUNT
#define LEFT_EYE_SURFACE_BASE (RIGHT_EYE_SURFACE_BASE + SM64_RIGHT_EYE_TRIANGLE_COUNT)
#define FEATURE_SURFACE_BASE (LEFT_EYE_SURFACE_BASE + SM64_LEFT_EYE_TRIANGLE_COUNT)
#define SURFACE_COUNT (FEATURE_SURFACE_BASE + FEATURE_TRIANGLE_COUNT)
#define COMMAND_COUNT (SURFACE_COUNT + 3U)
#define TITLE_BITMAP_BASE VDP2_VRAM_ADDR(0, 0x00000)
#define TITLE_BITMAP_WIDTH 512U
#define TITLE_BITMAP_HEIGHT 256U

static vdp1_gouraud_table_t gouraud[SM64_FACE_PRIMITIVE_COUNT];
static uint16_t draw_order[SURFACE_COUNT];
static int32_t surface_depths[SURFACE_COUNT];
static bool draw_order_ready;
static int32_t vertex_normals[SM64_FACE_VERTEX_COUNT][3];
static uint8_t diffuse_cache[SM64_FACE_VERTEX_COUNT];
static uint8_t shine_cache[SM64_FACE_VERTEX_COUNT];

typedef struct view_state {
    angle_t yaw;
    angle_t pitch;
    uint8_t projection_divisor;
    bool shine_enabled;
    bool auto_rotate;
    bool animation_enabled;
} view_state_t;

typedef struct point3 {
    int32_t x;
    int32_t y;
    int32_t z;
} point3_t;

static point3_t transformed_face[SM64_FACE_VERTEX_COUNT];
static int16_vec2_t projected_face[SM64_FACE_VERTEX_COUNT];
static int16_t deformed_face[SM64_FACE_VERTEX_COUNT][3];

static view_state_t view = { 0, 0, 6, true, false, true };
static fix16_t view_sin_yaw;
static fix16_t view_cos_yaw;
static fix16_t view_sin_pitch;
static fix16_t view_cos_pitch;
static uint16_t shade_build_ticks;
static uint16_t frame_ticks;
static uint16_t painter_sort_ticks;
static uint16_t command_build_ticks;
static uint16_t gouraud_upload_ticks;
static uint16_t render_wait_ticks;
static uint16_t deformation_ticks;
static uint16_t deformation_frame;
static vdp1_cmdt_list_t *command_list;
static bool controls_ready;
static bool pad_connected;
static uint16_t pad_down;
static uint16_t pad_edge;
static bool title_handoff;

static void build_vertex_normals(void);
static void build_lighting_cache(void);

static void
title_backdrop_init(void)
{
    /* VDP2 owns the title field while VDP1 keeps the deforming face.  This
     * deliberately uses a 512x256 RGB555 NBG1 bitmap, the Yaul equivalent of
     * the R11 background pattern; the ROM-derived converter will replace this
     * staging fill without changing the runtime layout. */
    volatile rgb1555_t *pixels = (volatile rgb1555_t *)TITLE_BITMAP_BASE;
    for (uint16_t y = 0; y < TITLE_BITMAP_HEIGHT; y++) {
        for (uint16_t x = 0; x < TITLE_BITMAP_WIDTH; x++) {
            const uint8_t wave = (uint8_t)(((x >> 4) ^ (y >> 3)) & 7U);
            /* Keep this deliberately obvious on an uncalibrated CRT/emulator
             * gamma curve: it is a staging field, not the eventual extracted
             * title texture. */
            const uint8_t blue = (uint8_t)(17U + (y >> 5) + wave);
            pixels[(uint32_t)y * TITLE_BITMAP_WIDTH + x] = RGB1555(1,
              (uint8_t)(3U + (wave >> 1)), (uint8_t)(6U + wave), blue);
        }
    }
    /* VDP2 VRAM is reached through the SH-2 cacheable bus mapping.  Unlike
     * Yaul's queued DMA helpers, this direct staging fill needs an explicit
     * write-back before the VDP2 fetch engine can see it. */
    cpu_cache_purge();
    const vdp2_scrn_bitmap_format_t title_format = {
        .scroll_screen = VDP2_SCRN_NBG1,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = TITLE_BITMAP_BASE,
    };
    /* A 512x256 RGB555 bitmap spans VDP2 banks A0 and A1.  The bitmap format
     * alone does not schedule those banks for NBG1 fetches; this is a close
     * port of Yaul's MIT vdp2-normal-bitmap cycle-allocation pattern. */
    const vdp2_vram_cycp_t title_cycles = {
        .pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
    };
    vdp2_vram_cycp_set(&title_cycles);
    vdp2_scrn_bitmap_format_set(&title_format);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 5);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG1);
}

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
        const int16_t *a = deformed_face[f[1]];
        const int16_t *b = deformed_face[f[2]];
        const int16_t *c = deformed_face[f[3]];
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

static void
update_view_trig(void)
{
    fix16_sincos(view.yaw, &view_sin_yaw, &view_cos_yaw);
    fix16_sincos(view.pitch, &view_sin_pitch, &view_cos_pitch);
}

static point3_t
transform_point(const int16_t *v)
{
    const int32_t x = (((int32_t)v[0] * view_cos_yaw) +
      ((int32_t)v[2] * view_sin_yaw)) >> 16;
    const int32_t z = ((-(int32_t)v[0] * view_sin_yaw) +
      ((int32_t)v[2] * view_cos_yaw)) >> 16;
    return (point3_t) {
        x,
        (((int32_t)v[1] * view_cos_pitch) - (z * view_sin_pitch)) >> 16,
        (((int32_t)v[1] * view_sin_pitch) + (z * view_cos_pitch)) >> 16
    };
}

static int16_vec2_t
project_transformed(point3_t point)
{
    return (int16_vec2_t)INT16_VEC2_INITIALIZER(
      160 + (point.x / view.projection_divisor),
      160 - (point.y / view.projection_divisor));
}

static void
update_face_transform_cache(void)
{
    for (uint16_t i = 0; i < SM64_FACE_VERTEX_COUNT; i++) {
        transformed_face[i] = transform_point(deformed_face[i]);
        projected_face[i] = project_transformed(transformed_face[i]);
    }
}

static void
update_face_deformation(void)
{
    const uint16_t right_frame = deformation_frame % SM64_RIGHT_EYELID_ANIMATION_FRAME_COUNT;
    const uint16_t left_frame = deformation_frame % SM64_LEFT_EYELID_ANIMATION_FRAME_COUNT;
    const int32_t right_delta = sm64_right_eyelid_animation[right_frame][2] -
      sm64_right_eyelid_animation[0][2];
    const int32_t left_delta = sm64_left_eyelid_animation[left_frame][2] -
      sm64_left_eyelid_animation[0][2];
    const uint16_t start = cpu_frt_count_get();
    for (uint16_t i = 0; i < SM64_FACE_VERTEX_COUNT; i++) {
        int32_t eyelid_offset = 0;
        if (view.animation_enabled) {
            /* The stream is direct GD_ANIM_ROT3S source data (0.1 degree
             * units); the weights are the original Goddard non-normalized
             * accumulation values. This first target-side evaluator keeps
             * the rotation-to-local-displacement scale explicit until full
             * joint matrices replace it. */
            eyelid_offset = ((right_delta * sm64_right_eyelid_weights[i]) +
              (left_delta * sm64_left_eyelid_weights[i])) / (32768 * 8);
        }
        deformed_face[i][0] = sm64_face_vertices[i][0];
        deformed_face[i][1] = (int16_t)(sm64_face_vertices[i][1] - eyelid_offset);
        deformed_face[i][2] = sm64_face_vertices[i][2];
    }
    build_vertex_normals();
    build_lighting_cache();
    deformation_ticks = (uint16_t)(cpu_frt_count_get() - start);
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

static uint8_t
vertex_shine(uint16_t index)
{
    const int32_t *normal = vertex_normals[index];
    /* Goddard's default material path projects a tiny 32x32 IA8 lobe using
     * generated normal coordinates. Approximate that narrow lobe in fixed
     * point, against the same upper-left white-star direction as our diffuse
     * pass, so it fits directly in Saturn VDP1 Gouraud endpoints. */
    const int32_t dot = abs32((-3 * normal[0]) + (4 * normal[1]) + (6 * normal[2]));
    const int32_t maximum = 6 * (abs32(normal[0]) + abs32(normal[1]) + abs32(normal[2]));
    if (maximum == 0)
        return 0;
    const int32_t alignment = (dot * 31) / maximum;
    if (alignment <= 25)
        return 0;
    const int32_t lobe = alignment - 24;
    return (uint8_t)((lobe * lobe * 31) / 49);
}

static void
build_lighting_cache(void)
{
    for (uint16_t i = 0; i < SM64_FACE_VERTEX_COUNT; i++) {
        diffuse_cache[i] = vertex_intensity(i);
        shine_cache[i] = vertex_shine(i);
    }
}

static rgb1555_t
shaded_material_color(uint16_t material, uint16_t index)
{
    const uint8_t *rgb = sm64_face_material_rgb[material & 7U];
    const uint8_t diffuse = diffuse_cache[index];
    const uint8_t shine = view.shine_enabled ? shine_cache[index] : 0U;
    const uint8_t red = (rgb[0] * diffuse) / 31U;
    const uint8_t green = (rgb[1] * diffuse) / 31U;
    const uint8_t blue = (rgb[2] * diffuse) / 31U;
    return RGB1555(1,
      red + (((31U - red) * shine) / 31U),
      green + (((31U - green) * shine) / 31U),
      blue + (((31U - blue) * shine) / 31U));
}

static void
rebuild_gouraud_tables(void)
{
    cpu_frt_count_set(0);
    for (uint16_t source = 0; source < SM64_FACE_PRIMITIVE_COUNT; source++) {
        const uint16_t *f = sm64_face_primitives[source];
        vdp1_gouraud_table_t *shade = &gouraud[source];
        shade->colors[0] = shaded_material_color(f[0], f[1]);
        shade->colors[1] = shaded_material_color(f[0], f[2]);
        shade->colors[2] = shaded_material_color(f[0], f[3]);
        shade->colors[3] = shaded_material_color(f[0], f[4]);
    }
    shade_build_ticks = cpu_frt_count_get();
}

static int16_vec2_t
project_point(const int16_t *v)
{
    /* Goddard face coordinates: X is horizontal and Y is vertical. */
    return project_transformed(transform_point(v));
}

static int16_vec2_t
project_eye_point(const int16_t *v, int16_t offset_x, int16_t offset_y,
  int16_t center_x, int16_t center_y)
{
    int16_vec2_t point = project_point(v);
    const int16_t center_object[3] = {
        (center_x - 160) * 6,
        (160 - center_y) * 6,
        0
    };
    const int16_vec2_t projected_center = project_point(center_object);
    /* The raw eye objects are too large against the source face's eye-surface
     * geometry at this camera. Reduce around the calibrated surface center. */
    point.x = projected_center.x +
      (((point.x + offset_x - projected_center.x) * 2) / 3);
    point.y = projected_center.y +
      (((point.y + offset_y - projected_center.y) * 2) / 3);
    return point;
}

static int16_vec2_t
project_feature_point(const int16_t *v, int16_t center_x, int16_t center_y,
  uint8_t scale_num, uint8_t scale_den, int16_t offset_x, int16_t offset_y)
{
    int16_vec2_t point = project_point(v);
    point.x = center_x + (((point.x - center_x) * scale_num) / scale_den) + offset_x;
    point.y = center_y + (((point.y - center_y) * scale_num) / scale_den) + offset_y;
    return point;
}

static int
feature_depth(const int16_t vertices[][3], const uint16_t *f)
{
    return (transform_point(vertices[f[1]]).z +
      transform_point(vertices[f[2]]).z +
      transform_point(vertices[f[3]]).z) / 3;
}

static int
eye_depth(const int16_t vertices[][3], const uint16_t *f)
{
    /* The Goddard eye objects are separate display-list geometry, while this
     * first Saturn target evaluates the face deformation independently.  Put
     * the complete eye skin fractionally ahead of the face so the white,
     * iris, and pupil cannot fall through it; then retain an explicit tiny
     * order for the pupil and glint.  A shared joint evaluator replaces this
     * temporary composition bias in M2. */
    int depth = feature_depth(vertices, f) + 48;
    if (f[0] == 2U)
        depth += 12; /* black pupil */
    else if (f[0] == 3U)
        depth += 24; /* white glint */
    return depth;
}

static int
depth_of(uint16_t surface)
{
    if (surface < FACE_SURFACE_COUNT) {
        const uint16_t *f = sm64_face_primitives[surface];
        return (transformed_face[f[1]].z + transformed_face[f[2]].z +
          transformed_face[f[3]].z + transformed_face[f[4]].z) / 4;
    }
    if (surface < LEFT_EYE_SURFACE_BASE)
        return eye_depth(sm64_right_eye_vertices,
          &sm64_right_eye_triangles[surface - RIGHT_EYE_SURFACE_BASE][0]);
    if (surface < FEATURE_SURFACE_BASE)
        return eye_depth(sm64_left_eye_vertices,
          &sm64_left_eye_triangles[surface - LEFT_EYE_SURFACE_BASE][0]);
    surface -= FEATURE_SURFACE_BASE;
    if (surface < SM64_RIGHT_EYEBROW_TRIANGLE_COUNT)
        return feature_depth(sm64_right_eyebrow_vertices, sm64_right_eyebrow_triangles[surface]);
    surface -= SM64_RIGHT_EYEBROW_TRIANGLE_COUNT;
    if (surface < SM64_LEFT_EYEBROW_TRIANGLE_COUNT)
        return feature_depth(sm64_left_eyebrow_vertices, sm64_left_eyebrow_triangles[surface]);
    surface -= SM64_LEFT_EYEBROW_TRIANGLE_COUNT;
    return feature_depth(sm64_mustache_vertices, sm64_mustache_triangles[surface]);
}

static void
sort_for_painter(void)
{
    /* Transform each surface only once. The original proof recomputed three
     * fixed-point vertex transforms for every insertion-sort comparison,
     * making a 1,049-surface frame needlessly quadratic in transform cost. */
    for (uint16_t i = 0; i < SURFACE_COUNT; i++)
        surface_depths[i] = depth_of(i);
    if (!draw_order_ready) {
        for (uint16_t i = 0; i < SURFACE_COUNT; i++)
            draw_order[i] = i;
        draw_order_ready = true;
    }
    /* Stable insertion sort is efficient here because camera motion is small
     * and the preceding frame's painter order is already nearly sorted. */
    for (uint16_t i = 1; i < SURFACE_COUNT; i++) {
        const uint16_t chosen = draw_order[i];
        const int32_t depth = surface_depths[chosen];
        uint16_t j = i;
        while (j > 0 && surface_depths[draw_order[j - 1]] > depth) {
            draw_order[j] = draw_order[j - 1];
            j--;
        }
        draw_order[j] = chosen;
    }
}

static void
draw_eye_triangle(vdp1_cmdt_t *cmdt, const int16_t vertices[][3],
  const uint16_t triangles[][4], uint16_t triangle,
  const uint8_t materials[][3], int16_t offset_x, int16_t offset_y,
  int16_t center_x, int16_t center_y)
{
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768 };
    const uint16_t *f = triangles[triangle];
    const uint8_t *rgb = materials[f[0] & 3U];
    const int16_vec2_t projected[4] = {
        project_eye_point(vertices[f[1]], offset_x, offset_y, center_x, center_y), project_eye_point(vertices[f[2]], offset_x, offset_y, center_x, center_y),
        project_eye_point(vertices[f[3]], offset_x, offset_y, center_x, center_y), project_eye_point(vertices[f[3]], offset_x, offset_y, center_x, center_y)
    };
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, mode);
    vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
    vdp1_cmdt_vtx_set(cmdt, projected);
}

static void
draw_feature_triangle(vdp1_cmdt_t *cmdt,
  const int16_t vertices[][3], const uint16_t triangles[][4],
  uint16_t triangle, rgb1555_t color, int16_t center_x, int16_t center_y,
  uint8_t scale_num, uint8_t scale_den, int16_t offset_x, int16_t offset_y)
{
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768 };
    const uint16_t *f = triangles[triangle];
    const int16_vec2_t projected[4] = {
        project_feature_point(vertices[f[1]], center_x, center_y, scale_num, scale_den, offset_x, offset_y),
        project_feature_point(vertices[f[2]], center_x, center_y, scale_num, scale_den, offset_x, offset_y),
        project_feature_point(vertices[f[3]], center_x, center_y, scale_num, scale_den, offset_x, offset_y),
        project_feature_point(vertices[f[3]], center_x, center_y, scale_num, scale_den, offset_x, offset_y)
    };
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, mode);
    vdp1_cmdt_color_set(cmdt, color);
    vdp1_cmdt_vtx_set(cmdt, projected);
}

static void
draw_source_face(bool shade_dirty)
{
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
    const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
    vdp1_vram_partitions_t partitions;
    if (command_list == NULL)
        return;
    const uint16_t sort_start = cpu_frt_count_get();
    update_face_transform_cache();
    sort_for_painter();
    painter_sort_ticks = (uint16_t)(cpu_frt_count_get() - sort_start);
    const uint16_t command_start = cpu_frt_count_get();
    vdp1_cmdt_list_t *list = command_list;
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
    vdp1_vram_partitions_get(&partitions);
    for (uint16_t out = 0; out < SURFACE_COUNT; out++) {
        uint16_t source = draw_order[out];
        vdp1_cmdt_t *cmdt = &list->cmdts[out + 2U];
        if (source >= RIGHT_EYE_SURFACE_BASE) {
            if (source < LEFT_EYE_SURFACE_BASE) {
                draw_eye_triangle(cmdt, sm64_right_eye_vertices, sm64_right_eye_triangles,
                  source - RIGHT_EYE_SURFACE_BASE, sm64_right_eye_material_rgb, 5, -4, 179, 128);
                continue;
            }
            if (source < FEATURE_SURFACE_BASE) {
                draw_eye_triangle(cmdt, sm64_left_eye_vertices, sm64_left_eye_triangles,
                  source - LEFT_EYE_SURFACE_BASE, sm64_left_eye_material_rgb, -6, -4, 139, 128);
                continue;
            }
            source -= FEATURE_SURFACE_BASE;
            if (source < SM64_RIGHT_EYEBROW_TRIANGLE_COUNT) {
                draw_feature_triangle(cmdt, sm64_right_eyebrow_vertices,
                  sm64_right_eyebrow_triangles, source, RGB1555(1, 0, 0, 0),
                  160, 160, 1, 1, 3, 0);
            } else if ((source -= SM64_RIGHT_EYEBROW_TRIANGLE_COUNT) < SM64_LEFT_EYEBROW_TRIANGLE_COUNT) {
                draw_feature_triangle(cmdt, sm64_left_eyebrow_vertices,
                  sm64_left_eyebrow_triangles, source, RGB1555(1, 0, 0, 0),
                  160, 160, 1, 1, -3, 0);
            } else {
                source -= SM64_LEFT_EYEBROW_TRIANGLE_COUNT;
                /* Preserve the source silhouette scale, placement, and black
                 * material. The common Z sort—not screen-space shrinking—is
                 * what makes the nose correctly occlude the moustache. */
                draw_feature_triangle(cmdt, sm64_mustache_vertices,
                  sm64_mustache_triangles, source, RGB1555(1, 0, 0, 0),
                  160, 160, 1, 1, 0, 0);
            }
            continue;
        }
        const uint16_t *f = sm64_face_primitives[source];
        const int16_vec2_t vertices[4] = {
            projected_face[f[1]], projected_face[f[2]],
            projected_face[f[3]], projected_face[f[4]]
        };
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, material_color(f[0], 31));
        vdp1_cmdt_vtx_set(cmdt, vertices);
        vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions.gouraud_base +
          (source * sizeof(vdp1_gouraud_table_t)));
    }
    vdp1_cmdt_end_set(&list->cmdts[SURFACE_COUNT + 2U]);
    command_build_ticks = (uint16_t)(cpu_frt_count_get() - command_start);
    if (shade_dirty) {
        const uint16_t upload_start = cpu_frt_count_get();
        scu_dma_transfer(0, (void *)partitions.gouraud_base, gouraud, sizeof(gouraud));
        scu_dma_transfer_wait(0);
        gouraud_upload_ticks = (uint16_t)(cpu_frt_count_get() - upload_start);
    } else {
        gouraud_upload_ticks = 0;
    }
    const uint16_t render_start = cpu_frt_count_get();
    vdp1_sync_cmdt_list_put(list, 0);
    vdp1_sync_render();
    vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
    render_wait_ticks = (uint16_t)(cpu_frt_count_get() - render_start);
}

static bool
update_controls(void)
{
    smpc_peripheral_digital_t digital;
    (void)memset(&digital, 0, sizeof(digital));
    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    pad_connected = digital.connected != 0;
    if (!pad_connected) {
        pad_down = 0;
        pad_edge = 0;
        return false;
    }
    /* The first populated SMPC sample can present every changed bit as an
     * edge relative to libyaul's zeroed history. Seed that history before
     * accepting one-shot controls such as A and B. */
    if (!controls_ready) {
        controls_ready = true;
        return false;
    }

    /* libyaul 0.3.1 names the current-down mask `pressed` and the newly
     * pressed edge mask `held`. Use semantic aliases here so continuous and
     * one-shot controls cannot be accidentally exchanged. */
    const uint16_t down = digital.pressed.raw;
    const uint16_t edge = digital.held.raw;
    pad_down = down;
    pad_edge = edge;
    if ((down & PERIPHERAL_DIGITAL_LEFT) != 0 && view.yaw > -8192)
        view.yaw -= 256;
    if ((down & PERIPHERAL_DIGITAL_RIGHT) != 0 && view.yaw < 8192)
        view.yaw += 256;
    if ((down & PERIPHERAL_DIGITAL_UP) != 0 && view.pitch > -5461)
        view.pitch -= 256;
    if ((down & PERIPHERAL_DIGITAL_DOWN) != 0 && view.pitch < 5461)
        view.pitch += 256;
    if ((edge & PERIPHERAL_DIGITAL_L) != 0 && view.projection_divisor < 8)
        view.projection_divisor++;
    if ((edge & PERIPHERAL_DIGITAL_R) != 0 && view.projection_divisor > 5)
        view.projection_divisor--;
    if ((edge & PERIPHERAL_DIGITAL_B) != 0)
        view.auto_rotate = !view.auto_rotate;
    if ((edge & PERIPHERAL_DIGITAL_C) != 0)
        view.animation_enabled = !view.animation_enabled;
    bool shade_dirty = false;
    if ((edge & PERIPHERAL_DIGITAL_A) != 0) {
        view.shine_enabled = !view.shine_enabled;
        shade_dirty = true;
    }
    if ((edge & PERIPHERAL_DIGITAL_START) != 0) {
        title_handoff = true;
        view.auto_rotate = false;
    }
    if (view.auto_rotate) {
        view.yaw += 96;
        if (view.yaw > 8192)
            view.yaw = -8192;
    }
    update_view_trig();
    return shade_dirty;
}

static void
update_hud(uint16_t frame)
{
    if ((frame % 10U) != 0)
        return;
    const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
    dbgio_printf("\x1B[HSM64 SATURN INTERACTIVE FACE\n"
      "PAD D-PAD CAM L/R ZOOM START HANDOFF\n"
      "YMIR: WASD Q/E J/K F=RESET  C=ANIM\n"
      "A SHINE:%s  B ORBIT:%s  C EYELIDS:%s\n"
      "FRAME %u TICKS  ~%u.%u FPS\n"
      "SHADE REBUILD %u TICKS (ON TOGGLE)\n"
      "CMD %u  QUAD %u  TRI %u\n"
      "DEFORM %u  SORT %u  BUILD %u  G-UP %u  WAIT %u\n"
      "PAD:%s DOWN %04X EDGE %04X   ",
      view.shine_enabled ? "ON " : "OFF",
      view.auto_rotate ? "ON " : "OFF",
      view.animation_enabled ? "ON " : "OFF",
      frame_ticks, fps_x10 / 10U, fps_x10 % 10U, shade_build_ticks,
      (uint16_t)(COMMAND_COUNT - 3U), (uint16_t)SM64_FACE_QUAD_COUNT,
      (uint16_t)(SM64_FACE_TRIANGLE_COUNT - (SM64_FACE_QUAD_COUNT * 2U)),
      deformation_ticks, painter_sort_ticks, command_build_ticks, gouraud_upload_ticks,
      render_wait_ticks,
      pad_connected ? "OK  " : "NONE", pad_down, pad_edge);
    dbgio_printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
      "              %s\n",
      title_handoff ? "CASTLE LOBBY HANDOFF (M1 PLACEHOLDER)" : "PRESS START");
    dbgio_flush();
}

static void
vblank_out_handler(void *work __unused)
{
    /* Schedule one asynchronous SMPC collection per video frame. The main
     * loop processes the completed sample on the following frame. */
    smpc_peripheral_intback_issue();
}

void
user_init(void)
{
    /* libyaul's controller examples initialize SMPC before registering the
     * VBlank callback that issues INTBACK. Reversing this order leaves the
     * demo displaying its first sample without receiving later updates. */
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 0, 0, 5));
    title_backdrop_init();
    vdp1_env_t env;
    vdp1_env_default_init(&env);
    /* A transparent VDP1 erase lets NBG1 supply the field where no VDP1
     * polygon exists. The face itself uses sprite priority 6 above NBG1. */
    env.erase_color = RGB1555(0, 0, 0, 0);
    vdp1_env_set(&env);
    vdp_sync_vblank_out_set(vblank_out_handler, NULL);
    /* Keep VDP1 one priority below dbgio's NBG3 plane so the live benchmark
     * HUD remains visible over the face. */
    for (uint8_t i = 0; i < 8; i++) vdp2_sprite_priority_set(i, 6);
    vdp2_tvmd_display_set();
    dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load();
    /* dbgio's Yaul VDP2 device owns NBG3, not NBG0.  Keep it visible above
     * the NBG1 title field and the VDP1 face so PRESS START is readable. */
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG1 | VDP2_SCRN_DISP_NBG3);
    command_list = vdp1_cmdt_list_alloc(COMMAND_COUNT);
    if (command_list == NULL) {
        dbgio_puts("SM64 SATURN\nCOMMAND LIST ALLOCATION FAILED");
        dbgio_flush();
        for (;;) {}
    }
    update_face_deformation();
    update_view_trig();
    rebuild_gouraud_tables();
    dbgio_puts("SM64 SATURN INTERACTIVE FACE\nINITIALIZING CONTROLS...");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    uint16_t frame = 0;
    for (;;) {
        const bool shade_dirty = update_controls();
        deformation_frame++;
        update_face_deformation();
        /* The face normals and compact VDP1 Gouraud tables follow every pose.
         * This is deliberately measured rather than assumed free. */
        rebuild_gouraud_tables();
        cpu_frt_count_set(0);
        draw_source_face(shade_dirty || frame == 0 || view.animation_enabled);
        frame_ticks = cpu_frt_count_get();
        update_hud(frame++);
        /* One controller transaction and one presentation per video frame.
         * The HUD's render ticks remain measured before this 60 Hz cap. */
        vdp2_tvmd_vblank_in_wait();
        vdp2_tvmd_vblank_out_wait();
    }
}

int main(void) { user_init(); return 0; }
