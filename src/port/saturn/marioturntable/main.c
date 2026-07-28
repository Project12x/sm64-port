/* M2 source Mario actor proof: generated geometry, neutral pose, VDP1 RGB fallback. */
#include <yaul.h>
#include <string.h>
#include "controller_saturn.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/mario.h"
#include "sm64.h"
#include "../gfx/saturn_mario_actor_mesh.h"
#include "mario_eye_uv_tiles.h"
#include "saturn_texture_residency.h"
#include "saturn_gouraud.h"
#include "saturn_vdp1_backend.h"
#define COMMAND_COUNT (SM64_MARIO_PRIMITIVE_COUNT + SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT + 3U)
#define DEPTH_BUCKET_COUNT 256U
#define NEAR_DEPTH 128
#define FAR_DEPTH 2048
/* mario_geo_body's source origin is at the feet rather than its visual center.
 * Keep that source-space convention and apply only the camera's framing offset. */
#define CAMERA_FRAME_Y 55
typedef struct { int32_t x, y, z; } point3_t;
static sm64_saturn_vdp1_backend_t vdp1_backend;
static sm64_saturn_texture_residency_t texture_residency;
static vdp1_gouraud_table_t gouraud[SM64_MARIO_PRIMITIVE_COUNT];
static int32_t vertex_normals[SM64_MARIO_VERTEX_COUNT][3];
static uint16_t draw_order[SM64_MARIO_PRIMITIVE_COUNT];
static int16_t bucket_head[DEPTH_BUCKET_COUNT], bucket_tail[DEPTH_BUCKET_COUNT];
static int16_t bucket_next[SM64_MARIO_PRIMITIVE_COUNT];
/* Eye normals face +Z, so the front camera stands on +Z and looks back. */
static angle_t yaw = 32768; static fix16_t sine_yaw, cosine_yaw;
static uint16_t frame_ticks, sort_ticks, build_ticks, visible_triangles, rejected_triangles; static bool controls_ready;
static uint16_t animation_frame;
static OSContPad source_pad;
static struct Controller source_controller;
static struct MarioState source_mario_state;
static struct Area source_area;
static struct Camera source_camera;
static int16_t projected_min_x, projected_min_y, projected_max_x, projected_max_y;
static int32_t min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }
static const int16_t *actor_vertex(uint16_t index) {
    return sm64_mario_animation_vertices[animation_frame][index];
}
static void build_vertex_normals(void) {
    (void)memset(vertex_normals, 0, sizeof(vertex_normals));
    for (uint16_t i = 0; i < SM64_MARIO_PRIMITIVE_COUNT; i++) {
        const uint16_t *p = sm64_mario_primitives[i];
        const int16_t *a = actor_vertex(p[1]), *b = actor_vertex(p[2]), *c = actor_vertex(p[3]);
        const int32_t ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const int32_t vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (uint8_t v = 1; v < 5; v++) {
            const uint16_t index = p[v];
            if (v == 4 && index == p[3]) continue;
            vertex_normals[index][0] += nx; vertex_normals[index][1] += ny; vertex_normals[index][2] += nz;
        }
    }
}
static rgb1555_t shaded_color(uint16_t material, uint16_t vertex) {
    const uint8_t *rgb = sm64_mario_material_rgb[material];
    const int32_t *n = vertex_normals[vertex];
    const int32_t maximum = 6 * (abs32(n[0]) + abs32(n[1]) + abs32(n[2]));
    const int32_t dot = (-2 * n[0]) + (4 * n[1]) + (5 * n[2]);
    const uint8_t intensity = maximum == 0 ? 20U : (uint8_t)(8 + ((dot > 0 ? dot : 0) * 23) / maximum);
    return RGB1555(1, (rgb[0] * intensity) / 31U,
                   (rgb[1] * intensity) / 31U,
                   (rgb[2] * intensity) / 31U);
}
static void rebuild_gouraud(void) {
    for (uint16_t i = 0; i < SM64_MARIO_PRIMITIVE_COUNT; i++) {
        const uint16_t *p = sm64_mario_primitives[i]; vdp1_gouraud_table_t *shade = &gouraud[i];
        shade->colors[0] = shaded_color(p[0], p[1]); shade->colors[1] = shaded_color(p[0], p[2]);
        shade->colors[2] = shaded_color(p[0], p[3]); shade->colors[3] = shaded_color(p[0], p[4]);
    }
}
static point3_t transform_point(const int16_t *s) {
    /* The source Animation/GeoLayout matrix walk yields standard SM64 world
     * coordinates: X horizontal, Y up, Z depth. Rotate about that source Y
     * axis; do not retain the earlier bind-mesh X-up camera workaround. */
    const int32_t x = (((int32_t)s[0] * cosine_yaw) + ((int32_t)s[2] * sine_yaw)) >> 16;
    const int32_t z = ((-(int32_t)s[0] * sine_yaw) + ((int32_t)s[2] * cosine_yaw)) >> 16;
    /* Close diagnostic framing only; the production path will consume the
     * original graph camera rather than promoting this turntable camera. */
    return (point3_t){ x, (int32_t)s[1] - CAMERA_FRAME_Y, z + 300 };
}
static int16_vec2_t project_point(point3_t p) {
    const int32_t z = p.z < 128 ? 128 : p.z;
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
      (int16_t)(160 + (p.x * 256) / z),
      (int16_t)(112 - (p.y * 256) / z));
    return result;
}
static void texture_tile_vertices(uint16_t tile, int16_vec2_t output[4]) {
    const uint16_t *indices = sm64_mario_textured_source_vertices[tile / 4U];
    const int16_t *a = actor_vertex(indices[0]);
    const int16_t *b = actor_vertex(indices[1]);
    const int16_t *c = actor_vertex(indices[2]);
    int16_t ab[3], bc[3], ca[3];
    for (uint8_t axis = 0; axis < 3; axis++) {
        ab[axis] = (int16_t)(((int32_t)a[axis] + b[axis]) / 2);
        bc[axis] = (int16_t)(((int32_t)b[axis] + c[axis]) / 2);
        ca[axis] = (int16_t)(((int32_t)c[axis] + a[axis]) / 2);
    }
    const int16_t *points[3];
    switch (tile & 3U) {
        case 0: points[0] = a;  points[1] = ab; points[2] = ca; break;
        case 1: points[0] = ab; points[1] = b;  points[2] = bc; break;
        case 2: points[0] = ca; points[1] = bc; points[2] = c;  break;
        default: points[0] = ab; points[1] = bc; points[2] = ca; break;
    }
    output[0] = project_point(transform_point(points[0]));
    output[1] = project_point(transform_point(points[1]));
    output[2] = project_point(transform_point(points[2]));
    output[3] = output[2];
}
static void update_view(void) {
    controller_saturn.read(&source_pad);
    if (!controls_ready) controls_ready = source_pad.errnum == 0;
    source_controller.rawStickX = source_pad.stick_x;
    source_controller.rawStickY = source_pad.stick_y;
    source_controller.buttonPressed = source_pad.button & (source_pad.button ^ source_controller.buttonDown);
    source_controller.buttonDown = source_pad.button;
    adjust_analog_stick(&source_controller);
    source_mario_state.input = 0;
    update_mario_button_inputs(&source_mario_state);
    update_mario_joystick_inputs(&source_mario_state);
    /* This diagnostic target does not invent a second movement model.  The
     * original SM64 joystick function owns intended magnitude/direction; the
     * renderer merely presents that source result as actor orientation until
     * the action/collision loop is linked. */
    if (controls_ready && (source_mario_state.input & INPUT_NONZERO_ANALOG))
        yaw = (angle_t)(32768 + source_mario_state.intendedYaw);
    fix16_sincos(yaw, &sine_yaw, &cosine_yaw);
}
static void sort_triangles(void) {
    const uint16_t start = cpu_frt_count_get();
    rejected_triangles = 0;
    projected_min_x = projected_min_y = 32767;
    projected_max_x = projected_max_y = -32768;
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKET_COUNT; bucket++)
        bucket_head[bucket] = bucket_tail[bucket] = -1;
    for (uint16_t i = 0; i < SM64_MARIO_PRIMITIVE_COUNT; i++) {
        const uint16_t *t = sm64_mario_primitives[i];
        bucket_next[i] = -2;
        const int32_t a = transform_point(actor_vertex(t[1])).z, b = transform_point(actor_vertex(t[2])).z, c = transform_point(actor_vertex(t[3])).z;
        const int32_t minimum = min3(a, b, c), maximum = max3(a, b, c);
        /* This is the libmic3d-style reject boundary. M2 will replace the
         * near intersection reject with source-preserving clipping. */
        if (minimum < NEAR_DEPTH || maximum > FAR_DEPTH) { rejected_triangles++; continue; }
        const int16_vec2_t pa = project_point(transform_point(actor_vertex(t[1])));
        const int16_vec2_t pb = project_point(transform_point(actor_vertex(t[2])));
        const int16_vec2_t pc = project_point(transform_point(actor_vertex(t[3])));
        if (pa.x < projected_min_x) projected_min_x = pa.x; if (pa.x > projected_max_x) projected_max_x = pa.x;
        if (pb.x < projected_min_x) projected_min_x = pb.x; if (pb.x > projected_max_x) projected_max_x = pb.x;
        if (pc.x < projected_min_x) projected_min_x = pc.x; if (pc.x > projected_max_x) projected_max_x = pc.x;
        if (pa.y < projected_min_y) projected_min_y = pa.y; if (pa.y > projected_max_y) projected_max_y = pa.y;
        if (pb.y < projected_min_y) projected_min_y = pb.y; if (pb.y > projected_max_y) projected_max_y = pb.y;
        if (pc.y < projected_min_y) projected_min_y = pc.y; if (pc.y > projected_max_y) projected_max_y = pc.y;
        const uint16_t bucket = (uint16_t)(((a + b + c) / 3 - NEAR_DEPTH) * (DEPTH_BUCKET_COUNT - 1U) / (FAR_DEPTH - NEAR_DEPTH));
        bucket_next[i] = -1;
        if (bucket_head[bucket] < 0) bucket_head[bucket] = i;
        else bucket_next[bucket_tail[bucket]] = i;
        bucket_tail[bucket] = i;
    }
    visible_triangles = 0;
    /* VDP1 has no Z buffer. Submit transformed far buckets first so the
     * articulated source face/eye patch is not hidden by later body commands.
     * Ordering within a coarse bucket remains stable source order. */
    for (int16_t bucket = DEPTH_BUCKET_COUNT - 1; bucket >= 0; bucket--)
        for (int16_t source = bucket_head[bucket]; source >= 0; source = bucket_next[source])
            draw_order[visible_triangles++] = (uint16_t)source;
    sort_ticks = (uint16_t)(cpu_frt_count_get() - start);
}
static void draw_mario(void) {
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD };
    vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
    const uint16_t start = cpu_frt_count_get();
    sm64_saturn_vdp1_backend_begin(&vdp1_backend);
    for (uint16_t out = 0; out < visible_triangles; out++) {
        const uint16_t primitive = draw_order[out];
        const uint16_t texture_tile_start = sm64_mario_texture_tile_start[primitive];
        if (texture_tile_start != SM64_MARIO_TEXTURE_TILE_NONE) {
            const uint16_t first_tile = (texture_tile_start / 4U) *
                SM64_MARIO_TEXTURE_TILES_PER_SOURCE;
            /* G_CC_BLENDRGBFADEA lowers to one shaded material polygon plus
             * the configured number of alpha-keyed texture-detail tiles.
             * Reserve and emit the pair atomically in source painter order. */
            vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(
                &vdp1_backend, 1U + SM64_MARIO_TEXTURE_TILES_PER_SOURCE);
            if (cmdt == NULL) continue;
            const uint16_t *t = sm64_mario_primitives[primitive];
            const int16_vec2_t base[4] = {
                project_point(transform_point(actor_vertex(t[1]))),
                project_point(transform_point(actor_vertex(t[2]))),
                project_point(transform_point(actor_vertex(t[3]))),
                project_point(transform_point(actor_vertex(t[4])))
            };
            vdp1_cmdt_polygon_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, mode);
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()
            });
            vdp1_cmdt_vtx_set(cmdt, base);
            vdp1_cmdt_gouraud_base_set(cmdt,
                (vdp1_vram_t)partitions.gouraud_base +
                primitive * sizeof(vdp1_gouraud_table_t));
            cmdt++;
            for (uint16_t tile = first_tile;
                 tile < first_tile + SM64_MARIO_TEXTURE_TILES_PER_SOURCE;
                 tile++) {
                int16_vec2_t v[4]; texture_tile_vertices(tile, v);
                vdp1_cmdt_distorted_sprite_set(cmdt);
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_REPLACE,
                    /* RGB1555 0x7FFF is valid transparent-white source data
                     * and VDP1's default end code. Generated tiles are fixed
                     * size, so disable end-code scanline termination. */
                    .end_code_disable = true
                });
                vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions.texture_base + tile * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t));
                vdp1_cmdt_char_size_set(cmdt, SM64_MARIO_TEXTURE_UV_TILE_WIDTH, SM64_MARIO_TEXTURE_UV_TILE_WIDTH);
                vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31)); vdp1_cmdt_vtx_set(cmdt, v);
                cmdt++;
            }
            continue;
        }
        const uint16_t *t = sm64_mario_primitives[primitive];
        const int16_vec2_t v[4] = { project_point(transform_point(actor_vertex(t[1]))), project_point(transform_point(actor_vertex(t[2]))), project_point(transform_point(actor_vertex(t[3]))), project_point(transform_point(actor_vertex(t[4]))) };
        vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(&vdp1_backend, 1);
        if (cmdt == NULL) continue;
        vdp1_cmdt_polygon_set(cmdt); vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = sm64_saturn_gouraud_neutral_color()
        });
        vdp1_cmdt_vtx_set(cmdt, v);
        vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions.gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
    }
    sm64_saturn_vdp1_backend_finish(&vdp1_backend);
    build_ticks = (uint16_t)(cpu_frt_count_get() - start);
    scu_dma_transfer(0, (void *)partitions.gouraud_base, gouraud, sizeof(gouraud)); scu_dma_transfer_wait(0);
    sm64_saturn_vdp1_backend_upload(&vdp1_backend); vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
}
static void vblank_out_handler(void *work __unused) { smpc_peripheral_intback_issue(); }
void user_init(void) {
    smpc_peripheral_init(); vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 4, 12));
    vdp1_env_t env; vdp1_env_default_init(&env); env.erase_color = RGB1555(1, 2, 4, 12); vdp1_env_set(&env);
    vdp_sync_vblank_out_set(vblank_out_handler, NULL);
    /* dbgio's NBG3 pattern plane has an opaque zero tile.  M2 uses the VDP1
     * top sprite priority so its source actor is not masked by that tile. */
    for (uint8_t priority = 0; priority < 8; priority++)
        vdp2_sprite_priority_set(priority, 7);
    vdp2_tvmd_display_set();
    dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load(); vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    {
        const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init(
                &vdp1_backend, COMMAND_COUNT, clip, local)) for (;;) {}
    }
    source_area.camera = &source_camera;
    source_mario_state.area = &source_area;
    source_mario_state.controller = &source_controller;
    source_mario_state.framesSinceA = 0xFF;
    source_mario_state.framesSinceB = 0xFF;
    source_mario_state.faceAngle[1] = 0;
    fix16_sincos(yaw, &sine_yaw, &cosine_yaw); build_vertex_normals(); rebuild_gouraud();
    { vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
      sm64_saturn_texture_residency_init(&texture_residency, &partitions);
      if (!sm64_saturn_texture_residency_upload(
              &texture_residency, 0, sm64_mario_texture_uv_tiles,
              sizeof(sm64_mario_texture_uv_tiles))) for (;;) {} }
    for (uint32_t frame = 0;; frame++) {
        update_view();
        const uint16_t next_animation_frame = (uint16_t)((frame / 2U) % SM64_MARIO_ANIMATION_FRAME_COUNT);
        if (next_animation_frame != animation_frame) {
            animation_frame = next_animation_frame; build_vertex_normals(); rebuild_gouraud();
        }
        cpu_frt_count_set(0); sort_triangles(); draw_mario(); frame_ticks = cpu_frt_count_get();
        if ((frame % 15U) == 0) { const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
            dbgio_printf("\x1B[HSM64 SATURN M2 — SOURCE MARIO IR\nmario_geo_body | %u tris -> %u quads + %u fallbacks\nPad %04X %d,%d -> Mario mag %d yaw %d input %04X | C5 %u/%u\nVISIBLE %u REJECTED %u | XY %d..%d / %d..%d\n~%u.%u FPS  SORT %u  BUILD %u  FRAME %u\n", (uint16_t)SM64_MARIO_TRIANGLE_COUNT, (uint16_t)SM64_MARIO_QUAD_COUNT, (uint16_t)(SM64_MARIO_PRIMITIVE_COUNT - SM64_MARIO_QUAD_COUNT), source_pad.button, source_pad.stick_x, source_pad.stick_y, (int16_t)source_mario_state.intendedMag, source_mario_state.intendedYaw, source_mario_state.input, animation_frame, (uint16_t)SM64_MARIO_ANIMATION_FRAME_COUNT, visible_triangles, rejected_triangles, projected_min_x, projected_max_x, projected_min_y, projected_max_y, fps_x10 / 10U, fps_x10 % 10U, sort_ticks, build_ticks, (uint16_t)frame);
            dbgio_flush(); vdp2_sync(); }
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
    }
}
int main(void) { user_init(); return 0; }
