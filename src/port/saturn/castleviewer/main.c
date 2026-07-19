/* M4 integration: actual SM64 Castle Area 1 plus source Mario actor/animation. */
#include <yaul.h>
#include <string.h>
#include "castle_area1.h"
#include "castle_gameplay_config.h"
#include "castle_graph_bridge.h"
#include "castle_uv_tiles.h"
#include "mario_actor_mesh.h"
#include "mario_eye_uv_tiles.h"

#define COMMAND_COUNT (SM64_CASTLE_UV_TILE_COUNT + (SM64_CASTLE_AREA1_TRIANGLE_COUNT - SM64_CASTLE_UV_TEXTURED_TRIANGLE_COUNT) + (SM64_MARIO_PRIMITIVE_COUNT - SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT) + SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT + 3U)
#define DRAW_ITEM_COUNT (SM64_CASTLE_AREA1_TRIANGLE_COUNT + SM64_MARIO_PRIMITIVE_COUNT)
#define DEPTH_BUCKETS 128U
#define NEAR_DEPTH 128
#define FAR_DEPTH 8192

typedef struct { int32_t x, y, z; } point3_t;
static vdp1_cmdt_list_t *command_list;
static vdp1_gouraud_table_t mario_gouraud[SM64_MARIO_PRIMITIVE_COUNT];
static int32_t mario_vertex_normals[SM64_MARIO_VERTEX_COUNT][3];
static int16_t bucket_head[DEPTH_BUCKETS], bucket_tail[DEPTH_BUCKETS];
static int16_t bucket_next[DRAW_ITEM_COUNT];
static uint16_t draw_order[DRAW_ITEM_COUNT];
static uint16_t visible_items, rejected_items, frame_ticks, animation_frame;
static angle_t mario_yaw = SM64_CASTLE_SPAWN_YAW;
static fix16_t mario_sine, mario_cosine;
static point3_t camera_position, camera_right, camera_up, camera_forward;
static sm64_saturn_castle_graph_state_t source_graph;

static int32_t min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }

static uint32_t isqrt_u64(uint64_t value) {
    uint64_t root = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

static point3_t normalize_q16(point3_t value) {
    const uint32_t length = isqrt_u64((uint64_t)((int64_t)value.x * value.x) +
                                      (uint64_t)((int64_t)value.y * value.y) +
                                      (uint64_t)((int64_t)value.z * value.z));
    if (length == 0) return (point3_t){0, 0, 0};
    return (point3_t){(int32_t)(((int64_t)value.x << 16) / length),
                      (int32_t)(((int64_t)value.y << 16) / length),
                      (int32_t)(((int64_t)value.z << 16) / length)};
}

static void update_source_camera(void) {
    const point3_t mario = {SM64_CASTLE_SPAWN_X, SM64_CASTLE_SPAWN_Y, SM64_CASTLE_SPAWN_Z};
    camera_position.x = SM64_CASTLE_CAMERA_BASE_X +
        (((mario.x - SM64_CASTLE_CAMERA_BASE_X) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    camera_position.y = SM64_CASTLE_SPAWN_FLOOR_Y + SM64_CASTLE_CAMERA_BASE_Y;
    camera_position.z = SM64_CASTLE_CAMERA_BASE_Z +
        (((mario.z - SM64_CASTLE_CAMERA_BASE_Z) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    const point3_t focus = {mario.x, mario.y + SM64_CASTLE_CAMERA_FOCUS_Y, mario.z};
    camera_forward = normalize_q16((point3_t){focus.x - camera_position.x,
                                              focus.y - camera_position.y,
                                              focus.z - camera_position.z});
    camera_right = normalize_q16((point3_t){-camera_forward.z, 0, camera_forward.x});
    camera_up = (point3_t){
        (int32_t)((-(int64_t)camera_right.z * camera_forward.y) >> 16),
        (int32_t)((((int64_t)camera_right.z * camera_forward.x) -
                   ((int64_t)camera_right.x * camera_forward.z)) >> 16),
        (int32_t)(((int64_t)camera_right.x * camera_forward.y) >> 16)
    };
}

static point3_t world_to_view(int32_t x, int32_t y, int32_t z) {
#ifdef SM64_SATURN_TEXTURE_PROBE_CAMERA
    /* Renderer validation only: reproduce the accepted M3 establishing view
     * so texture-state changes can be compared without conflating the known
     * rejected standalone camera transplant. The default build still follows
     * the source-state camera path until the original graph camera is linked. */
    return (point3_t){x + 1050, y - 720, z + 4200};
#else
    const point3_t relative = {x - camera_position.x, y - camera_position.y, z - camera_position.z};
    return (point3_t){
        (int32_t)((((int64_t)relative.x * camera_right.x) + ((int64_t)relative.y * camera_right.y) + ((int64_t)relative.z * camera_right.z)) >> 16),
        (int32_t)((((int64_t)relative.x * camera_up.x) + ((int64_t)relative.y * camera_up.y) + ((int64_t)relative.z * camera_up.z)) >> 16),
        (int32_t)((((int64_t)relative.x * camera_forward.x) + ((int64_t)relative.y * camera_forward.y) + ((int64_t)relative.z * camera_forward.z)) >> 16)
    };
#endif
}
static point3_t castle_point(const int16_t *source) {
    return world_to_view(source[0], source[1], source[2]);
}
static const int16_t *mario_vertex(uint16_t index) {
    return sm64_mario_animation_vertices[animation_frame][index];
}
static point3_t mario_point(const int16_t *source) {
    const int32_t x = (((int32_t)source[0] * mario_cosine) + ((int32_t)source[2] * mario_sine)) >> 16;
    const int32_t z = ((-(int32_t)source[0] * mario_sine) + ((int32_t)source[2] * mario_cosine)) >> 16;
    return world_to_view(x + SM64_CASTLE_SPAWN_X,
                         (int32_t)source[1] + SM64_CASTLE_SPAWN_Y,
                         z + SM64_CASTLE_SPAWN_Z);
}
static int16_vec2_t project_point(point3_t point) {
    const int32_t z = point.z < NEAR_DEPTH ? NEAR_DEPTH : point.z;
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
        (int16_t)(160 + (point.x * SM64_CASTLE_CAMERA_FOCAL_LENGTH) / z),
        (int16_t)(112 - (point.y * SM64_CASTLE_CAMERA_FOCAL_LENGTH) / z));
    return result;
}

static void build_mario_gouraud(void) {
    (void)memset(mario_vertex_normals, 0, sizeof(mario_vertex_normals));
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const int16_t *a = mario_vertex(indices[1]), *b = mario_vertex(indices[2]), *c = mario_vertex(indices[3]);
        const int32_t ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const int32_t vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (uint8_t corner = 1; corner < 5; corner++) {
            const uint16_t vertex = indices[corner];
            if (corner == 4 && vertex == indices[3]) continue;
            mario_vertex_normals[vertex][0] += nx;
            mario_vertex_normals[vertex][1] += ny;
            mario_vertex_normals[vertex][2] += nz;
        }
    }
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
        for (uint8_t corner = 0; corner < 4; corner++) {
            const int32_t *normal = mario_vertex_normals[indices[corner + 1]];
            const int32_t maximum = 6 * (abs32(normal[0]) + abs32(normal[1]) + abs32(normal[2]));
            const int32_t dot = (-2 * normal[0]) + (4 * normal[1]) + (5 * normal[2]);
            const uint8_t intensity = maximum == 0 ? 20U : (uint8_t)(8 + ((dot > 0 ? dot : 0) * 23) / maximum);
            mario_gouraud[primitive].colors[corner] = RGB1555(1,
                (rgb[0] * intensity) / 31U,
                (rgb[1] * intensity) / 31U,
                (rgb[2] * intensity) / 31U);
        }
    }
}

static void mario_texture_tile_vertices(uint16_t tile, int16_vec2_t output[4]) {
    const uint16_t *indices = sm64_mario_textured_source_vertices[tile / 4U];
    const int16_t *a = mario_vertex(indices[0]), *b = mario_vertex(indices[1]), *c = mario_vertex(indices[2]);
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
    output[0] = project_point(mario_point(points[0]));
    output[1] = project_point(mario_point(points[1]));
    output[2] = project_point(mario_point(points[2]));
    output[3] = output[2];
}

static void sort_scene(void) {
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKETS; bucket++) bucket_head[bucket] = bucket_tail[bucket] = -1;
    rejected_items = 0;
    for (uint16_t item = 0; item < DRAW_ITEM_COUNT; item++) {
        point3_t a, b, c;
        if (item < SM64_CASTLE_AREA1_TRIANGLE_COUNT) {
            const uint8_t root = sm64_castle_area1_triangle_root[item];
            if ((source_graph.selected_root_mask & (1U << root)) == 0U) {
                rejected_items++;
                bucket_next[item] = -2;
                continue;
            }
            const uint16_t *triangle = sm64_castle_area1_triangles[item];
            a = castle_point(sm64_castle_area1_vertices[triangle[0]]);
            b = castle_point(sm64_castle_area1_vertices[triangle[1]]);
            c = castle_point(sm64_castle_area1_vertices[triangle[2]]);
        } else {
            const uint16_t primitive = item - SM64_CASTLE_AREA1_TRIANGLE_COUNT;
            const uint16_t *indices = sm64_mario_primitives[primitive];
            a = mario_point(mario_vertex(indices[1]));
            b = mario_point(mario_vertex(indices[2]));
            c = mario_point(mario_vertex(indices[3]));
        }
        const int32_t minimum = min3(a.z, b.z, c.z), maximum = max3(a.z, b.z, c.z);
        if (minimum < NEAR_DEPTH || maximum > FAR_DEPTH) { rejected_items++; bucket_next[item] = -2; continue; }
        /* A centroid key lets one long wall triangle paint over geometry that
         * is wholly in front of its far edge.  The PS1 port's ordering-table
         * path keys opaque polygons by their farthest transformed vertex;
         * retain that hardware-oriented behavior here while preserving the
         * source display-list order within equal buckets. */
        const uint16_t bucket = (uint16_t)((maximum - NEAR_DEPTH) *
            (DEPTH_BUCKETS - 1U) / (FAR_DEPTH - NEAR_DEPTH));
        bucket_next[item] = -1;
        if (bucket_head[bucket] < 0) bucket_head[bucket] = (int16_t)item;
        else bucket_next[bucket_tail[bucket]] = (int16_t)item;
        bucket_tail[bucket] = (int16_t)item;
    }
    visible_items = 0;
    for (int16_t bucket = DEPTH_BUCKETS - 1; bucket >= 0; bucket--)
        for (int16_t item = bucket_head[bucket]; item >= 0; item = bucket_next[item]) draw_order[visible_items++] = (uint16_t)item;
}

static uint16_t draw_castle(uint16_t source, uint16_t command, const vdp1_vram_partitions_t *partitions) {
    const uint8_t layer = sm64_castle_area1_triangle_layer[source];
    const vdp1_cmdt_cc_t color_calculation =
        layer == SM64_CASTLE_LAYER_TRANSPARENT_DECAL
            ? VDP1_CMDT_CC_HALF_TRANSPARENT
            : VDP1_CMDT_CC_REPLACE;
    const uint16_t texture_start = sm64_castle_uv_tile_start[source];
    if (texture_start != SM64_CASTLE_UV_TILE_NONE) {
        for (uint16_t tile = texture_start; tile < texture_start + sm64_castle_uv_tile_count[source]; tile++) {
            const int16_vec2_t vertices[4] = {
                project_point(castle_point(sm64_castle_uv_positions[tile][0])),
                project_point(castle_point(sm64_castle_uv_positions[tile][1])),
                project_point(castle_point(sm64_castle_uv_positions[tile][2])),
                project_point(castle_point(sm64_castle_uv_positions[tile][2]))
            };
            vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
            vdp1_cmdt_distorted_sprite_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = color_calculation
            });
            vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions->texture_base + tile * SM64_CASTLE_UV_TILE_WIDTH * SM64_CASTLE_UV_TILE_WIDTH * sizeof(uint16_t));
            vdp1_cmdt_char_size_set(cmdt, SM64_CASTLE_UV_TILE_WIDTH, SM64_CASTLE_UV_TILE_WIDTH);
            vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
            vdp1_cmdt_vtx_set(cmdt, vertices);
        }
        return command;
    }
    const uint16_t *triangle = sm64_castle_area1_triangles[source];
    const int16_vec2_t vertices[4] = {
        project_point(castle_point(sm64_castle_area1_vertices[triangle[0]])),
        project_point(castle_point(sm64_castle_area1_vertices[triangle[1]])),
        project_point(castle_point(sm64_castle_area1_vertices[triangle[2]])),
        project_point(castle_point(sm64_castle_area1_vertices[triangle[2]]))
    };
    vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = VDP1_CMDT_CM_RGB_32768,
        .cc_mode = color_calculation
    });
    vdp1_cmdt_color_set(cmdt, RGB1555(1, 20, 20, 25));
    vdp1_cmdt_vtx_set(cmdt, vertices);
    return command;
}

static uint16_t draw_mario(uint16_t primitive, uint16_t command, const vdp1_vram_partitions_t *partitions) {
    const uint16_t texture_start = sm64_mario_texture_tile_start[primitive];
    if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE) {
        for (uint16_t tile = texture_start; tile < texture_start + 4U; tile++) {
            int16_vec2_t vertices[4]; mario_texture_tile_vertices(tile, vertices);
            vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
            vdp1_cmdt_distorted_sprite_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){.color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions->texture_base + sizeof(sm64_castle_uv_tiles) + tile * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t));
            vdp1_cmdt_char_size_set(cmdt, SM64_MARIO_TEXTURE_UV_TILE_WIDTH, SM64_MARIO_TEXTURE_UV_TILE_WIDTH);
            vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
            vdp1_cmdt_vtx_set(cmdt, vertices);
            vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions->gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
        }
        return command;
    }
    const uint16_t *indices = sm64_mario_primitives[primitive];
    const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
    const int16_vec2_t vertices[4] = {
        project_point(mario_point(mario_vertex(indices[1]))),
        project_point(mario_point(mario_vertex(indices[2]))),
        project_point(mario_point(mario_vertex(indices[3]))),
        project_point(mario_point(mario_vertex(indices[4])))
    };
    vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){.color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
    vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
    vdp1_cmdt_vtx_set(cmdt, vertices);
    vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions->gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
    return command;
}

static void draw_scene(void) {
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223), local = INT16_VEC2_INITIALIZER(0, 0);
    vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
    command_list->count = COMMAND_COUNT;
    (void)memset(command_list->cmdts, 0, sizeof(vdp1_cmdt_t) * command_list->count);
    vdp1_cmdt_system_clip_coord_set(&command_list->cmdts[0]); vdp1_cmdt_vtx_system_clip_coord_set(&command_list->cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&command_list->cmdts[1]); vdp1_cmdt_vtx_local_coord_set(&command_list->cmdts[1], local);
    uint16_t command = 2;
    /* N64 LAYER_ALPHA is binary cutout geometry that relies on the RDP's
     * Z-buffer. VDP1 has no Z-buffer, so opaque, alpha-test, and Mario must
     * share one far-to-near ordering pass. Only the genuinely translucent
     * decal root is submitted afterward with half-transparency. */
    for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint16_t output = 0; output < visible_items; output++) {
            const uint16_t item = draw_order[output];
            if (item < SM64_CASTLE_AREA1_TRIANGLE_COUNT) {
                const uint8_t layer = sm64_castle_area1_triangle_layer[item];
                const bool translucent =
                    layer == SM64_CASTLE_LAYER_TRANSPARENT_DECAL;
                if ((pass == 0 && !translucent) || (pass == 1 && translucent))
                    command = draw_castle(item, command, &partitions);
            } else if (pass == 0) {
                command = draw_mario(item - SM64_CASTLE_AREA1_TRIANGLE_COUNT,
                                     command, &partitions);
            }
        }
    }
    vdp1_cmdt_end_set(&command_list->cmdts[command]);
    scu_dma_transfer(0, (void *)partitions.gouraud_base, mario_gouraud, sizeof(mario_gouraud)); scu_dma_transfer_wait(0);
    vdp1_sync_cmdt_list_put(command_list, 0); vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
}

void user_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 4, 12));
    vdp1_env_t env; vdp1_env_default_init(&env); env.erase_color = RGB1555(1, 2, 4, 12); vdp1_env_set(&env);
    for (uint8_t priority = 0; priority < 8; priority++) vdp2_sprite_priority_set(priority, 7);
    vdp2_tvmd_display_set(); dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load(); vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    source_graph = sm64_saturn_castle_graph_init();
    if (!source_graph.valid) for (;;) {}
    command_list = vdp1_cmdt_list_alloc(COMMAND_COUNT); if (command_list == NULL) for (;;) {}
    fix16_sincos(mario_yaw, &mario_sine, &mario_cosine);
    update_source_camera();
    build_mario_gouraud();
    {
        vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
        if (sizeof(sm64_castle_uv_tiles) + sizeof(sm64_mario_texture_uv_tiles) > partitions.texture_size) for (;;) {}
        scu_dma_transfer(0, partitions.texture_base, sm64_castle_uv_tiles, sizeof(sm64_castle_uv_tiles)); scu_dma_transfer_wait(0);
        scu_dma_transfer(0, (uint8_t *)partitions.texture_base + sizeof(sm64_castle_uv_tiles), sm64_mario_texture_uv_tiles, sizeof(sm64_mario_texture_uv_tiles)); scu_dma_transfer_wait(0);
    }
    for (uint32_t frame = 0;; frame++) {
        const uint16_t next_frame = (uint16_t)((frame / 2U) % SM64_MARIO_ANIMATION_FRAME_COUNT);
        if (next_frame != animation_frame) { animation_frame = next_frame; build_mario_gouraud(); }
        cpu_frt_count_set(0); sort_scene(); draw_scene(); frame_ticks = cpu_frt_count_get();
        if ((frame % 15U) == 0) {
            const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
            dbgio_printf("\x1B[HSM64 SATURN M4 — SOURCE MARIO IN CASTLE\ngraph %u lists: O%u A%u D%u roots %02X | source spawn %d,%d,%d\nanim_C5 %u/%u | layered painter %u/%u\ntextures %lu + %lu bytes | ~%u.%u FPS\n",
                source_graph.display_lists, source_graph.opaque_lists,
                source_graph.alpha_lists, source_graph.decal_lists,
                source_graph.selected_root_mask,
                SM64_CASTLE_SPAWN_X, SM64_CASTLE_SPAWN_Y, SM64_CASTLE_SPAWN_Z,
                animation_frame, (uint16_t)SM64_MARIO_ANIMATION_FRAME_COUNT, visible_items, (uint16_t)DRAW_ITEM_COUNT,
                (uint32_t)sizeof(sm64_castle_uv_tiles), (uint32_t)sizeof(sm64_mario_texture_uv_tiles), fps_x10 / 10U, fps_x10 % 10U);
            dbgio_flush(); vdp2_sync();
        }
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
    }
}
int main(void) { user_init(); return 0; }
