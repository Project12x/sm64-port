/* M2 source Mario actor proof: generated geometry, neutral pose, VDP1 RGB fallback. */
#include <yaul.h>
#include <string.h>
#include "mario_actor_mesh.h"
#define COMMAND_COUNT (SM64_MARIO_TRIANGLE_COUNT + 3U)
#define DEPTH_BUCKET_COUNT 64U
#define NEAR_DEPTH 128
#define FAR_DEPTH 2048
/* mario_geo_body's source origin is at the feet rather than its visual center.
 * Keep that source-space convention and apply only the camera's framing offset. */
#define CAMERA_FRAME_Y 230
typedef struct { int32_t x, y, z; } point3_t;
static vdp1_cmdt_list_t *command_list;
static uint16_t draw_order[SM64_MARIO_TRIANGLE_COUNT];
static int16_t bucket_head[DEPTH_BUCKET_COUNT], bucket_tail[DEPTH_BUCKET_COUNT];
static int16_t bucket_next[SM64_MARIO_TRIANGLE_COUNT];
static angle_t yaw; static fix16_t sine_yaw, cosine_yaw;
static uint16_t frame_ticks, sort_ticks, build_ticks, visible_triangles, rejected_triangles; static bool controls_ready;
static int16_t projected_min_x, projected_min_y, projected_max_x, projected_max_y;
static int32_t min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static point3_t transform_point(const int16_t *s) {
    const int32_t x = (((int32_t)s[0] * cosine_yaw) + ((int32_t)s[2] * sine_yaw)) >> 16;
    const int32_t z = ((-(int32_t)s[0] * sine_yaw) + ((int32_t)s[2] * cosine_yaw)) >> 16;
    return (point3_t){ x, (int32_t)s[1] - CAMERA_FRAME_Y, z + 900 };
}
static int16_vec2_t project_point(point3_t p) {
    const int32_t z = p.z < 128 ? 128 : p.z;
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
      (int16_t)(160 + (p.x * 256) / z),
      (int16_t)(112 - (p.y * 256) / z));
    return result;
}
static void update_view(void) {
    smpc_peripheral_digital_t digital; (void)memset(&digital, 0, sizeof(digital));
    smpc_peripheral_process(); smpc_peripheral_digital_port(1, &digital);
    if (!controls_ready) controls_ready = digital.connected != 0;
    else if ((digital.pressed.raw & PERIPHERAL_DIGITAL_LEFT) != 0) yaw -= 192;
    else if ((digital.pressed.raw & PERIPHERAL_DIGITAL_RIGHT) != 0) yaw += 192;
    fix16_sincos(yaw, &sine_yaw, &cosine_yaw);
}
static void sort_triangles(void) {
    const uint16_t start = cpu_frt_count_get();
    rejected_triangles = 0;
    projected_min_x = projected_min_y = 32767;
    projected_max_x = projected_max_y = -32768;
    for (uint8_t bucket = 0; bucket < DEPTH_BUCKET_COUNT; bucket++)
        bucket_head[bucket] = bucket_tail[bucket] = -1;
    for (uint16_t i = 0; i < SM64_MARIO_TRIANGLE_COUNT; i++) {
        const int16_t *t = sm64_mario_triangles[i];
        bucket_next[i] = -2;
        const int32_t a = transform_point(&t[3]).z, b = transform_point(&t[6]).z, c = transform_point(&t[9]).z;
        const int32_t minimum = min3(a, b, c), maximum = max3(a, b, c);
        /* This is the libmic3d-style reject boundary. M2 will replace the
         * near intersection reject with source-preserving clipping. */
        if (minimum < NEAR_DEPTH || maximum > FAR_DEPTH) { rejected_triangles++; continue; }
        const int16_vec2_t pa = project_point(transform_point(&t[3]));
        const int16_vec2_t pb = project_point(transform_point(&t[6]));
        const int16_vec2_t pc = project_point(transform_point(&t[9]));
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
    /* The direct source-triangle adapter is temporarily represented by a
     * degenerate VDP1 quad. Its fill edge is not order-independent, so depth
     * buckets can let one dark degenerate primitive obscure the whole actor.
     * Preserve source command order until the M2 quadifier emits proper VDP1
     * polygons; bucket construction remains measured above as the future path. */
    for (uint16_t source = 0; source < SM64_MARIO_TRIANGLE_COUNT; source++)
        if (bucket_next[source] != -2)
            draw_order[visible_triangles++] = source;
    sort_ticks = (uint16_t)(cpu_frt_count_get() - start);
}
static void draw_mario(void) {
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223), local = INT16_VEC2_INITIALIZER(0, 0);
    const vdp1_cmdt_draw_mode_t mode = { .color_mode = VDP1_CMDT_CM_RGB_32768 };
    const uint16_t start = cpu_frt_count_get(); vdp1_cmdt_list_t *list = command_list; list->count = COMMAND_COUNT;
    (void)memset(list->cmdts, 0, sizeof(vdp1_cmdt_t) * list->count);
    vdp1_cmdt_system_clip_coord_set(&list->cmdts[0]); vdp1_cmdt_vtx_system_clip_coord_set(&list->cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&list->cmdts[1]); vdp1_cmdt_vtx_local_coord_set(&list->cmdts[1], local);
    for (uint16_t out = 0; out < visible_triangles; out++) {
        const int16_t *t = sm64_mario_triangles[draw_order[out]];
        const int16_vec2_t v[4] = { project_point(transform_point(&t[3])), project_point(transform_point(&t[6])), project_point(transform_point(&t[9])), project_point(transform_point(&t[9])) };
        vdp1_cmdt_t *cmdt = &list->cmdts[out + 2U]; vdp1_cmdt_polygon_set(cmdt); vdp1_cmdt_draw_mode_set(cmdt, mode);
        vdp1_cmdt_color_set(cmdt, RGB1555(1, t[0], t[1], t[2])); vdp1_cmdt_vtx_set(cmdt, v);
    }
    vdp1_cmdt_end_set(&list->cmdts[visible_triangles + 2U]); build_ticks = (uint16_t)(cpu_frt_count_get() - start);
    vdp1_sync_cmdt_list_put(list, 0); vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
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
    command_list = vdp1_cmdt_list_alloc(COMMAND_COUNT); if (command_list == NULL) for (;;) {}
    fix16_sincos(yaw, &sine_yaw, &cosine_yaw);
    for (uint32_t frame = 0;; frame++) {
        update_view(); cpu_frt_count_set(0); sort_triangles(); draw_mario(); frame_ticks = cpu_frt_count_get();
        if ((frame % 15U) == 0) { const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
            dbgio_printf("\x1B[HSM64 SATURN M2 — SOURCE MARIO ACTOR\nmario_geo_body / model.inc.c | %u source triangles\nD-PAD LEFT/RIGHT: orbit | depth buckets active\nVISIBLE %u REJECTED %u | XY %d..%d / %d..%d\n~%u.%u FPS  SORT %u  BUILD %u  FRAME %u\n", (uint16_t)SM64_MARIO_TRIANGLE_COUNT, visible_triangles, rejected_triangles, projected_min_x, projected_max_x, projected_min_y, projected_max_y, fps_x10 / 10U, fps_x10 % 10U, sort_ticks, build_ticks, (uint16_t)frame);
            dbgio_flush(); vdp2_sync(); }
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
    }
}
int main(void) { user_init(); return 0; }
