/* M3 fixed-camera proof: actual SM64 Castle Area 1 opaque source root. */
#include <yaul.h>
#include <string.h>
#include "castle_area1_opaque.h"
#include "castle_uv_tiles.h"

#define COMMAND_COUNT (SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT + SM64_CASTLE_UV_TILE_COUNT + 3U)
#define DEPTH_BUCKETS 128U
#define NEAR_DEPTH 128
#define FAR_DEPTH 8192
typedef struct { int32_t x, y, z; } point3_t;
static vdp1_cmdt_list_t *command_list;
static vdp1_gouraud_table_t gouraud[SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT];
static int16_t bucket_head[DEPTH_BUCKETS], bucket_tail[DEPTH_BUCKETS];
static int16_t bucket_next[SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT];
static uint16_t draw_order[SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT];
static uint16_t visible_triangles, rejected_triangles, frame_ticks;

static int32_t min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static int32_t abs32(int32_t n) { return n < 0 ? -n : n; }

static point3_t transform_point(const int16_t *source) {
    /* Fixed M3 establishing view: looking into the real root bank around its
     * source-space center. This intentionally stays independent of a new
     * level/camera framework until visibility and clipping are proven. */
    const int32_t x = (int32_t)source[0] + 1050;
    const int32_t y = (int32_t)source[1] - 720;
    const int32_t z = (int32_t)source[2] + 4200;
    return (point3_t){x, y, z};
}
static int16_vec2_t project_point(point3_t p) {
    const int32_t z = p.z < NEAR_DEPTH ? NEAR_DEPTH : p.z;
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
        (int16_t)(160 + (p.x * 300) / z),
        (int16_t)(112 - (p.y * 300) / z));
    return result;
}
static rgb1555_t shaded_color(uint16_t index, uint8_t corner) {
    const uint16_t *tri = sm64_castle_area1_triangles[index];
    const int16_t *a = sm64_castle_area1_vertices[tri[0]], *b = sm64_castle_area1_vertices[tri[1]], *c = sm64_castle_area1_vertices[tri[2]];
    const int32_t ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const int32_t vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const int32_t denominator = abs32(nx) + abs32(ny) + abs32(nz);
    const int32_t light = denominator == 0 ? 12 : 6 + ((nx + 2 * ny + 2 * nz > 0 ? nx + 2 * ny + 2 * nz : 0) * 22) / (4 * denominator);
    const int32_t height = sm64_castle_area1_vertices[tri[corner]][1] + 50;
    const uint8_t tone = (uint8_t)(light > 31 ? 31 : light);
    const uint8_t marble = (uint8_t)(height > 1000 ? 22 : 17);
    return RGB1555(1, (marble * tone) / 31U, (marble * tone) / 31U, ((marble + 5U) * tone) / 31U);
}
static void build_gouraud(void) {
    for (uint16_t i = 0; i < SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT; i++) {
        for (uint8_t corner = 0; corner < 3; corner++) gouraud[i].colors[corner] = shaded_color(i, corner);
        gouraud[i].colors[3] = gouraud[i].colors[2];
    }
}
static void sort_triangles(void) {
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKETS; bucket++) bucket_head[bucket] = bucket_tail[bucket] = -1;
    rejected_triangles = 0;
    for (uint16_t i = 0; i < SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT; i++) {
        const uint16_t *tri = sm64_castle_area1_triangles[i];
        const int32_t a = transform_point(sm64_castle_area1_vertices[tri[0]]).z, b = transform_point(sm64_castle_area1_vertices[tri[1]]).z, c = transform_point(sm64_castle_area1_vertices[tri[2]]).z;
        const int32_t minimum = min3(a, b, c), maximum = max3(a, b, c);
        if (minimum < NEAR_DEPTH || maximum > FAR_DEPTH) { rejected_triangles++; bucket_next[i] = -2; continue; }
        const uint16_t bucket = (uint16_t)(((a + b + c) / 3 - NEAR_DEPTH) * (DEPTH_BUCKETS - 1U) / (FAR_DEPTH - NEAR_DEPTH));
        bucket_next[i] = -1;
        if (bucket_head[bucket] < 0) bucket_head[bucket] = i; else bucket_next[bucket_tail[bucket]] = i;
        bucket_tail[bucket] = i;
    }
    visible_triangles = 0;
    for (int16_t bucket = DEPTH_BUCKETS - 1; bucket >= 0; bucket--)
        for (int16_t source = bucket_head[bucket]; source >= 0; source = bucket_next[source]) draw_order[visible_triangles++] = (uint16_t)source;
}
static void draw_scene(void) {
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223), local = INT16_VEC2_INITIALIZER(0, 0);
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD };
    vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
    command_list->count = COMMAND_COUNT; (void)memset(command_list->cmdts, 0, sizeof(vdp1_cmdt_t) * command_list->count);
    vdp1_cmdt_system_clip_coord_set(&command_list->cmdts[0]); vdp1_cmdt_vtx_system_clip_coord_set(&command_list->cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&command_list->cmdts[1]); vdp1_cmdt_vtx_local_coord_set(&command_list->cmdts[1], local);
    uint16_t command = 2;
    for (uint16_t out = 0; out < visible_triangles; out++) {
        const uint16_t index = draw_order[out], *tri = sm64_castle_area1_triangles[index];
        const uint16_t texture_tile_start = sm64_castle_uv_tile_start[index];
        if (texture_tile_start != SM64_CASTLE_UV_TILE_NONE) {
            /* Source-selected Fast3D material: replace this source triangle
             * in the same painter slot with its four pre-baked affine VDP1
             * tiles. This is the same generic path used by source Mario. */
            for (uint16_t tile = texture_tile_start;
                 tile < texture_tile_start + SM64_CASTLE_UV_TILES_PER_TRIANGLE; tile++) {
                const int16_vec2_t v[4] = {
                    project_point(transform_point(sm64_castle_uv_positions[tile][0])),
                    project_point(transform_point(sm64_castle_uv_positions[tile][1])),
                    project_point(transform_point(sm64_castle_uv_positions[tile][2])),
                    project_point(transform_point(sm64_castle_uv_positions[tile][2]))
                };
                vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
                vdp1_cmdt_distorted_sprite_set(cmdt);
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_GOURAUD
                });
                vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions.texture_base +
                    tile * SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH * sizeof(uint16_t));
                vdp1_cmdt_char_size_set(cmdt, SM64_CASTLE_UV_TILE_WIDTH, SM64_CASTLE_UV_TILE_WIDTH);
                vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
                vdp1_cmdt_vtx_set(cmdt, v);
                vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions.gouraud_base +
                    index * sizeof(vdp1_gouraud_table_t));
            }
            continue;
        }
        const int16_vec2_t a = project_point(transform_point(sm64_castle_area1_vertices[tri[0]]));
        const int16_vec2_t b = project_point(transform_point(sm64_castle_area1_vertices[tri[1]]));
        const int16_vec2_t c = project_point(transform_point(sm64_castle_area1_vertices[tri[2]]));
        const int16_vec2_t v[4] = {a, b, c, c};
        vdp1_cmdt_polygon_set(&command_list->cmdts[command]); vdp1_cmdt_draw_mode_set(&command_list->cmdts[command], mode);
        vdp1_cmdt_color_set(&command_list->cmdts[command], RGB1555(1, 20, 20, 25)); vdp1_cmdt_vtx_set(&command_list->cmdts[command], v);
        vdp1_cmdt_gouraud_base_set(&command_list->cmdts[command++], (vdp1_vram_t)partitions.gouraud_base + index * sizeof(vdp1_gouraud_table_t));
    }
    vdp1_cmdt_end_set(&command_list->cmdts[command]);
    scu_dma_transfer(0, (void *)partitions.gouraud_base, gouraud, sizeof(gouraud)); scu_dma_transfer_wait(0);
    vdp1_sync_cmdt_list_put(command_list, 0); vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
}
void user_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 4, 12));
    vdp1_env_t env; vdp1_env_default_init(&env); env.erase_color = RGB1555(1, 2, 4, 12); vdp1_env_set(&env);
    for (uint8_t priority = 0; priority < 8; priority++) vdp2_sprite_priority_set(priority, 7);
    vdp2_tvmd_display_set(); dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load(); vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    command_list = vdp1_cmdt_list_alloc(COMMAND_COUNT); if (command_list == NULL) for (;;) {}
    build_gouraud();
    { vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
      scu_dma_transfer(0, (void *)partitions.texture_base, sm64_castle_uv_tiles,
          sizeof(sm64_castle_uv_tiles)); scu_dma_transfer_wait(0); }
    for (uint32_t frame = 0;; frame++) {
        cpu_frt_count_set(0); sort_triangles(); draw_scene(); frame_ticks = cpu_frt_count_get();
        if ((frame % 15U) == 0) { const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
            dbgio_printf("\x1B[HSM64 SATURN M3 — CASTLE AREA 1 ROOT\nactual source opaque bank | fixed camera\n%u source tris | %u visible | %u rejected\n~%u.%u FPS | source texture slice active\n", (uint16_t)SM64_CASTLE_AREA1_OPAQUE_TRIANGLE_COUNT, visible_triangles, rejected_triangles, fps_x10 / 10U, fps_x10 % 10U); dbgio_flush(); vdp2_sync(); }
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
    }
}
int main(void) { user_init(); return 0; }
