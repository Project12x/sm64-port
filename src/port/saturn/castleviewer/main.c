/* M4 integration: actual SM64 Castle Area 1 plus source Mario actor/animation. */
#include <yaul.h>
#include <string.h>
#include "sm64.h"
#include "controller_saturn.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/mario.h"
#include "castle_area1.h"
#include "castle_gameplay_config.h"
#include "castle_graph_bridge.h"
#include "castle_uv_tiles.h"
#include "mario_actor_mesh.h"
#include "mario_eye_uv_tiles.h"

#define COMMAND_COUNT (SM64_CASTLE_UV_TILE_COUNT + (SM64_MARIO_PRIMITIVE_COUNT - SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT) + SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT + 3U)
#define DRAW_ITEM_COUNT (SM64_CASTLE_UV_TILE_COUNT + SM64_MARIO_PRIMITIVE_COUNT)
#define DEPTH_BUCKETS 128U
#define NEAR_DEPTH 128
#define FAR_DEPTH 8192
#define VDP1_COORD_MIN (-1024)
#define VDP1_COORD_MAX 1023
/* A single VDP1 distorted sprite is not a safe representation for a source
 * polygon whose projected extent is much larger than the target viewport.
 * Keep the source tile/UV data intact, but reject that primitive until the
 * host lowering can split it with interpolated attributes. */
#define MAX_PROJECTED_SPAN 640

typedef struct { int32_t x, y, z; } point3_t;
static vdp1_cmdt_list_t *command_list;
static vdp1_gouraud_table_t mario_gouraud[SM64_MARIO_PRIMITIVE_COUNT];
static int32_t mario_vertex_normals[SM64_MARIO_VERTEX_COUNT][3];
static int16_t mario_bucket_head[DEPTH_BUCKETS], mario_bucket_tail[DEPTH_BUCKETS];
static int16_t mario_bucket_next[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t mario_order[SM64_MARIO_PRIMITIVE_COUNT], mario_visible;
static uint16_t draw_order[DRAW_ITEM_COUNT];
static uint16_t visible_items, rejected_items, frame_ticks, animation_frame;
static angle_t mario_yaw = SM64_CASTLE_SPAWN_YAW;
static fix16_t mario_sine, mario_cosine;
static point3_t camera_position, camera_right, camera_up, camera_forward;
static sm64_saturn_castle_graph_state_t source_graph;
static OSContPad source_pad;
static struct Controller source_controller;
static struct MarioState source_mario_state;
static struct Area source_area;
static struct Camera source_camera;
static int32_t mario_world_x = SM64_CASTLE_SPAWN_X;
static int32_t mario_world_y = SM64_CASTLE_SPAWN_Y;
static int32_t mario_world_z = SM64_CASTLE_SPAWN_Z;
static bool controls_ready;

_Static_assert(SM64_CASTLE_UV_TEXTURED_PRIMITIVE_COUNT == SM64_CASTLE_AREA1_PRIMITIVE_COUNT,
               "Castle tile painter requires the complete source material bank");

static int32_t min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static int32_t min4(int32_t a, int32_t b, int32_t c, int32_t d) { const int32_t abc = min3(a, b, c); return abc < d ? abc : d; }
static int32_t max4(int32_t a, int32_t b, int32_t c, int32_t d) { const int32_t abc = max3(a, b, c); return abc > d ? abc : d; }
static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }
static int32_t clamp32(int32_t value, int32_t minimum, int32_t maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

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
    const point3_t mario = {mario_world_x, mario_world_y, mario_world_z};
    camera_position.x = SM64_CASTLE_CAMERA_BASE_X +
        (((mario.x - SM64_CASTLE_CAMERA_BASE_X) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    camera_position.y = SM64_CASTLE_SPAWN_FLOOR_Y + SM64_CASTLE_CAMERA_BASE_Y;
    camera_position.z = SM64_CASTLE_CAMERA_BASE_Z +
        (((mario.z - SM64_CASTLE_CAMERA_BASE_Z) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    /* Source `update_fixed_camera()` first applies
     * calc_y_to_curr_floor(..., focMul=0.9f), then adds the 125-unit focus
     * height. Keep that floor-relative aim instead of looking at a bespoke
     * fixed Y coordinate. */
    const int32_t floor_focus_offset =
        (int32_t)(((int64_t)(SM64_CASTLE_SPAWN_FLOOR_Y - mario.y) *
                   SM64_CASTLE_CAMERA_FOCUS_FLOOR_SCALE_Q16) >> 16);
    const point3_t focus = {mario.x,
                            mario.y + floor_focus_offset + SM64_CASTLE_CAMERA_FOCUS_Y,
                            mario.z};
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
#if defined(SM64_SATURN_TEXTURE_PROBE_CAMERA) || defined(SM64_SATURN_TEXTURE_PROBE_PROJECTION)
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
static point3_t painter_camera_position(void) {
#if defined(SM64_SATURN_TEXTURE_PROBE_CAMERA) || defined(SM64_SATURN_TEXTURE_PROBE_PAINTER)
    return (point3_t){-1050, 720, -4200};
#else
    return camera_position;
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
    return world_to_view(x + mario_world_x,
                         (int32_t)source[1] + mario_world_y,
                         z + mario_world_z);
}

static void update_source_input(void) {
    controller_saturn.read(&source_pad);
    if (!controls_ready) controls_ready = source_pad.errnum == 0;
    source_controller.rawStickX = source_pad.stick_x;
    source_controller.rawStickY = source_pad.stick_y;
    source_controller.buttonPressed = source_pad.button &
        (source_pad.button ^ source_controller.buttonDown);
    source_controller.buttonDown = source_pad.button;
    adjust_analog_stick(&source_controller);
    source_mario_state.input = 0;
    update_mario_button_inputs(&source_mario_state);
    update_mario_joystick_inputs(&source_mario_state);
    source_camera.yaw = 0;
    if ((source_mario_state.input & INPUT_NONZERO_ANALOG) != 0U) {
        mario_yaw = (angle_t)(32768 + source_mario_state.intendedYaw);
        fix16_sincos(mario_yaw, &mario_sine, &mario_cosine);
        /* The original SM64 joystick routine owns magnitude and intended
         * direction. This bridge advances the source actor in source units
         * until collision/action execution is linked into this target. */
        const int32_t speed = (int32_t)source_mario_state.intendedMag / 4;
        mario_world_x += ((int32_t)mario_sine * speed) >> 16;
        mario_world_z += ((int32_t)mario_cosine * speed) >> 16;
    }
    update_source_camera();
}
static int16_vec2_t project_point(point3_t point) {
    const int32_t z = point.z < NEAR_DEPTH ? NEAR_DEPTH : point.z;
    const int32_t screen_x = 160 + (point.x * SM64_CASTLE_CAMERA_FOCAL_LENGTH) / z;
    const int32_t screen_y = 112 - (point.y * SM64_CASTLE_CAMERA_FOCAL_LENGTH) / z;
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
        (int16_t)clamp32(screen_x, VDP1_COORD_MIN, VDP1_COORD_MAX),
        (int16_t)clamp32(screen_y, VDP1_COORD_MIN, VDP1_COORD_MAX));
    return result;
}

static bool quad_intersects_viewport(point3_t a, point3_t b, point3_t c, point3_t d) {
    const point3_t points[4] = {a, b, c, d};
    int32_t minimum_x = INT32_MAX, minimum_y = INT32_MAX;
    int32_t maximum_x = INT32_MIN, maximum_y = INT32_MIN;
    for (uint8_t corner = 0; corner < 4; corner++) {
        /* VDP1 has no homogeneous clipper. Clamping a vertex behind the
         * near plane turns a floor polygon crossing the camera into a giant
         * wedge, making the lobby emblem appear beyond the doors. Reject it
         * until the source polygon can be split with its UVs preserved. */
        if (points[corner].z < NEAR_DEPTH)
            return false;
        const int32_t x = 160 + (points[corner].x * SM64_CASTLE_CAMERA_FOCAL_LENGTH) /
                                  points[corner].z;
        const int32_t y = 112 - (points[corner].y * SM64_CASTLE_CAMERA_FOCAL_LENGTH) /
                                  points[corner].z;
        if (x < minimum_x) minimum_x = x;
        if (x > maximum_x) maximum_x = x;
        if (y < minimum_y) minimum_y = y;
        if (y > maximum_y) maximum_y = y;
    }
    if (maximum_x - minimum_x > MAX_PROJECTED_SPAN ||
        maximum_y - minimum_y > MAX_PROJECTED_SPAN)
        return false;
    return maximum_x >= 0 && minimum_x <= 319 && maximum_y >= 0 && minimum_y <= 223;
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

static bool tile_is_visible(uint16_t tile) {
    const uint16_t primitive = sm64_castle_uv_tile_primitive[tile];
    const uint8_t root = sm64_castle_area1_primitive_root[primitive];
    if ((source_graph.selected_root_mask & (1U << root)) == 0U) return false;
    const point3_t a = castle_point(sm64_castle_uv_positions[tile][0]);
    const point3_t b = castle_point(sm64_castle_uv_positions[tile][1]);
    const point3_t c = castle_point(sm64_castle_uv_positions[tile][2]);
    const point3_t d = castle_point(sm64_castle_uv_positions[tile][3]);
    const int32_t minimum = min4(a.z, b.z, c.z, d.z);
    const int32_t maximum = max4(a.z, b.z, c.z, d.z);
    return minimum >= NEAR_DEPTH && maximum <= FAR_DEPTH &&
           quad_intersects_viewport(a, b, c, d);
}

static void append_static_tile(uint16_t tile) {
    if (tile_is_visible(tile)) draw_order[visible_items++] = tile;
    else rejected_items++;
}

static void sort_mario(void) {
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKETS; bucket++)
        mario_bucket_head[bucket] = mario_bucket_tail[bucket] = -1;
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const point3_t a = mario_point(mario_vertex(indices[1]));
        const point3_t b = mario_point(mario_vertex(indices[2]));
        const point3_t c = mario_point(mario_vertex(indices[3]));
        const point3_t d = mario_point(mario_vertex(indices[4]));
        const int32_t minimum = min4(a.z, b.z, c.z, d.z);
        const int32_t maximum = max4(a.z, b.z, c.z, d.z);
        if (minimum < NEAR_DEPTH || maximum > FAR_DEPTH ||
            !quad_intersects_viewport(a, b, c, d)) {
            mario_bucket_next[primitive] = -2;
            rejected_items++;
            continue;
        }
        const uint16_t bucket = (uint16_t)((maximum - NEAR_DEPTH) *
            (DEPTH_BUCKETS - 1U) / (FAR_DEPTH - NEAR_DEPTH));
        mario_bucket_next[primitive] = -1;
        if (mario_bucket_head[bucket] < 0) mario_bucket_head[bucket] = (int16_t)primitive;
        else mario_bucket_next[mario_bucket_tail[bucket]] = (int16_t)primitive;
        mario_bucket_tail[bucket] = (int16_t)primitive;
    }
    mario_visible = 0;
    for (int16_t bucket = DEPTH_BUCKETS - 1; bucket >= 0; bucket--)
        for (int16_t primitive = mario_bucket_head[bucket]; primitive >= 0;
             primitive = mario_bucket_next[primitive])
            mario_order[mario_visible++] = (uint16_t)primitive;
}

static void append_mario(void) {
    for (uint16_t index = 0; index < mario_visible; index++)
        draw_order[visible_items++] = SM64_CASTLE_UV_TILE_COUNT + mario_order[index];
}

static int64_t bsp_side(uint16_t node, point3_t point) {
    return ((int64_t)sm64_castle_bsp_normal[node][0] * point.x) +
           ((int64_t)sm64_castle_bsp_normal[node][1] * point.y) +
           ((int64_t)sm64_castle_bsp_normal[node][2] * point.z) +
           sm64_castle_bsp_distance[node];
}

static void traverse_bsp(int16_t node, bool insert_mario) {
    if (node < 0) {
        if (insert_mario) append_mario();
        return;
    }
    const bool camera_front = bsp_side((uint16_t)node, painter_camera_position()) >= 0;
    const bool mario_front = bsp_side((uint16_t)node, (point3_t){
        mario_world_x, mario_world_y, mario_world_z}) >= 0;
    const int16_t front = sm64_castle_bsp_children[node][0];
    const int16_t back = sm64_castle_bsp_children[node][1];
    const int16_t far = camera_front ? back : front;
    const int16_t near = camera_front ? front : back;
    const bool mario_in_far = camera_front ? !mario_front : mario_front;
    traverse_bsp(far, insert_mario && mario_in_far);
    for (uint16_t offset = 0; offset < sm64_castle_bsp_tile_range[node][1]; offset++)
        append_static_tile(sm64_castle_bsp_tile_range[node][0] + offset);
    traverse_bsp(near, insert_mario && !mario_in_far);
}

static void sort_scene(void) {
    rejected_items = 0;
    visible_items = 0;
    sort_mario();
    traverse_bsp(0, true);
    /* True translucent decals remain a deliberately late source-derived pass. */
    for (uint16_t tile = SM64_CASTLE_BSP_DECAL_START;
         tile < SM64_CASTLE_BSP_DECAL_START + SM64_CASTLE_BSP_DECAL_COUNT; tile++)
        append_static_tile(tile);
}

static uint16_t draw_castle(uint16_t tile, uint16_t command, const vdp1_vram_partitions_t *partitions) {
    const uint16_t primitive = sm64_castle_uv_tile_primitive[tile];
    const uint8_t layer = sm64_castle_area1_primitive_layer[primitive];
    const vdp1_cmdt_cc_t color_calculation =
        layer == SM64_CASTLE_LAYER_TRANSPARENT_DECAL
            ? VDP1_CMDT_CC_HALF_TRANSPARENT
            : VDP1_CMDT_CC_REPLACE;
    const int16_vec2_t vertices[4] = {
        project_point(castle_point(sm64_castle_uv_positions[tile][0])),
        project_point(castle_point(sm64_castle_uv_positions[tile][1])),
        project_point(castle_point(sm64_castle_uv_positions[tile][2])),
        project_point(castle_point(sm64_castle_uv_positions[tile][3]))
    };
    vdp1_cmdt_t *cmdt = &command_list->cmdts[command++];
    vdp1_cmdt_distorted_sprite_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
            ? VDP1_CMDT_CM_CLUT_16 : VDP1_CMDT_CM_RGB_32768,
        .cc_mode = color_calculation,
        .end_code_disable = SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
    });
    vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions->texture_base +
                            tile * SM64_CASTLE_UV_TILE_BYTES);
    vdp1_cmdt_char_size_set(cmdt, SM64_CASTLE_UV_TILE_WIDTH, SM64_CASTLE_UV_TILE_WIDTH);
#if SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
    vdp1_cmdt_color_mode1_set(cmdt,
        (vdp1_vram_t)&partitions->clut_base[sm64_castle_uv_tile_clut[tile]]);
#else
    vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
#endif
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
            if (item < SM64_CASTLE_UV_TILE_COUNT) {
                const uint16_t primitive = sm64_castle_uv_tile_primitive[item];
                const uint8_t layer = sm64_castle_area1_primitive_layer[primitive];
                const bool translucent =
                    layer == SM64_CASTLE_LAYER_TRANSPARENT_DECAL;
                if ((pass == 0 && !translucent) || (pass == 1 && translucent))
                    command = draw_castle(item, command, &partitions);
            } else if (pass == 0) {
                command = draw_mario(item - SM64_CASTLE_UV_TILE_COUNT,
                                     command, &partitions);
            }
        }
    }
    vdp1_cmdt_end_set(&command_list->cmdts[command]);
    scu_dma_transfer(0, (void *)partitions.gouraud_base, mario_gouraud, sizeof(mario_gouraud)); scu_dma_transfer_wait(0);
    vdp1_sync_cmdt_list_put(command_list, 0); vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
}

static void vblank_out_handler(void *work __unused) {
    smpc_peripheral_intback_issue();
}

void user_init(void) {
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 4, 12));
    vdp1_env_t env; vdp1_env_default_init(&env); env.erase_color = RGB1555(1, 2, 4, 12); vdp1_env_set(&env);
    for (uint8_t priority = 0; priority < 8; priority++) vdp2_sprite_priority_set(priority, 7);
    vdp2_tvmd_display_set(); dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC); dbgio_dev_font_load(); vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    vdp_sync_vblank_out_set(vblank_out_handler, NULL);
    /* Prime the first INTBACK collection. The callback supplies subsequent
     * frames, but a target that starts polling before the first VBlank can
     * otherwise retain an all-zero, disconnected OSContPad sample. */
    smpc_peripheral_intback_issue();
    source_graph = sm64_saturn_castle_graph_init();
    if (!source_graph.valid) for (;;) {}
    vdp1_vram_partitions_set(COMMAND_COUNT,
        sizeof(sm64_castle_uv_tiles) + sizeof(sm64_mario_texture_uv_tiles),
        SM64_MARIO_PRIMITIVE_COUNT, SM64_CASTLE_UV_CLUT_COUNT);
    command_list = vdp1_cmdt_list_alloc(COMMAND_COUNT); if (command_list == NULL) for (;;) {}
    source_area.camera = &source_camera;
    source_mario_state.area = &source_area;
    source_mario_state.controller = &source_controller;
    source_mario_state.framesSinceA = 0xFF;
    source_mario_state.framesSinceB = 0xFF;
    source_mario_state.faceAngle[1] = mario_yaw;
    source_camera.yaw = 0;
    fix16_sincos(mario_yaw, &mario_sine, &mario_cosine);
    update_source_camera();
    build_mario_gouraud();
    {
        vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
        if (sizeof(sm64_castle_uv_tiles) + sizeof(sm64_mario_texture_uv_tiles) > partitions.texture_size) for (;;) {}
#if SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
        if (sizeof(sm64_castle_uv_cluts) > partitions.clut_size) for (;;) {}
        scu_dma_transfer(0, partitions.clut_base, sm64_castle_uv_cluts,
                         sizeof(sm64_castle_uv_cluts));
        scu_dma_transfer_wait(0);
#endif
        scu_dma_transfer(0, partitions.texture_base, sm64_castle_uv_tiles, sizeof(sm64_castle_uv_tiles)); scu_dma_transfer_wait(0);
        scu_dma_transfer(0, (uint8_t *)partitions.texture_base + sizeof(sm64_castle_uv_tiles), sm64_mario_texture_uv_tiles, sizeof(sm64_mario_texture_uv_tiles)); scu_dma_transfer_wait(0);
    }
    for (uint32_t frame = 0;; frame++) {
        update_source_input();
        const uint16_t next_frame = (uint16_t)((frame / 2U) % SM64_MARIO_ANIMATION_FRAME_COUNT);
        if (next_frame != animation_frame) { animation_frame = next_frame; build_mario_gouraud(); }
        cpu_frt_count_set(0); sort_scene(); draw_scene(); frame_ticks = cpu_frt_count_get();
        if ((frame % 15U) == 0) {
            const uint32_t fps_x10 = frame_ticks == 0 ? 0 : 33528000UL / frame_ticks;
            dbgio_printf("\x1B[HSM64 SATURN M4 — SOURCE MARIO IN CASTLE\ngraph %u lists: O%u A%u D%u roots %02X | source spawn %d,%d,%d\nanim_C5 %u/%u | painter %u/%u | VDP1 quads %u\ntextures %lu + %lu bytes | ~%u.%u FPS\n",
                source_graph.display_lists, source_graph.opaque_lists,
                source_graph.alpha_lists, source_graph.decal_lists,
                source_graph.selected_root_mask,
                mario_world_x, mario_world_y, mario_world_z,
                animation_frame, (uint16_t)SM64_MARIO_ANIMATION_FRAME_COUNT, visible_items, (uint16_t)DRAW_ITEM_COUNT,
                (uint16_t)SM64_CASTLE_UV_PAIRED_QUAD_COUNT,
                (uint32_t)sizeof(sm64_castle_uv_tiles), (uint32_t)sizeof(sm64_mario_texture_uv_tiles), fps_x10 / 10U, fps_x10 % 10U);
            dbgio_flush(); vdp2_sync();
        }
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
    }
}
int main(void) { user_init(); return 0; }
