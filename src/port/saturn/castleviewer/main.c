/* M4 integration: actual SM64 Castle Area 1 plus source Mario actor/animation. */
#include <yaul.h>
#include <string.h>
#include "sm64.h"
#include "controller_saturn.h"
#include "saturn_cart_bank.h"
#include "saturn_frame_profile.h"
#include "saturn_render_queue.h"
#include "saturn_transform.h"
#include "saturn_texture_residency.h"
#include "saturn_vdp1_backend.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/mario.h"
#include "castle_area1.h"
#include "castle_gameplay_config.h"
#include "castle_collision.h"
#include "castle_graph_bridge.h"
#include "castle_uv_tiles.h"
#include "mario_actor_mesh.h"
#include "mario_eye_uv_tiles.h"
#include "engine/surface_load.h"
#include "engine/surface_collision.h"
#include "engine/math_util.h"

#define COMMAND_COUNT (SM64_CASTLE_UV_TILE_COUNT + (SM64_MARIO_PRIMITIVE_COUNT - SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT) + SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT + 3U)
#define DRAW_ITEM_COUNT (SM64_CASTLE_UV_TILE_COUNT + SM64_MARIO_PRIMITIVE_COUNT)
#define DEPTH_BUCKETS 128U
#define SCENE_DEPTH_BUCKETS 64U
#define NEAR_DEPTH 128
#define FAR_DEPTH 8192
#define VDP1_COORD_MIN (-1024)
#define VDP1_COORD_MAX 1023
/* A single VDP1 distorted sprite is not a safe representation for a source
 * polygon whose projected extent is much larger than the target viewport.
 * Keep the source tile/UV data intact, but reject that primitive until the
 * host lowering can split it with interpolated attributes. */
#define MAX_PROJECTED_SPAN 640
#define GOURAUD_UPDATE_PERIOD 2U
#define FRAME_STATS_PERIOD 30U
#define CART_STAGE_CHUNK 8192U
/* NTSC 320-wide master SH-2 clock (26.8224 MHz), FRT /128, times ten for
 * the one-decimal FPS display.  The former /8 timer wrapped every ~19.5 ms
 * and therefore reported nonsense for the observed ~125 ms Castle frame. */
#define FRT_TICKS_PER_SECOND_X10 2095500UL
#define SOURCE_BANK_CASTLE_AREA1 1U
#define SOURCE_BANK_MARIO 2U

typedef sm64_saturn_vec3i_t point3_t;
static sm64_saturn_vdp1_backend_t vdp1_backend;
static sm64_saturn_texture_residency_t texture_residency;
static vdp1_gouraud_table_t mario_gouraud[SM64_MARIO_PRIMITIVE_COUNT];
static int32_t mario_vertex_normals[SM64_MARIO_VERTEX_COUNT][3];
static int16_t mario_bucket_head[DEPTH_BUCKETS], mario_bucket_tail[DEPTH_BUCKETS];
static int16_t mario_bucket_next[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t mario_order[SM64_MARIO_PRIMITIVE_COUNT], mario_visible;
static point3_t mario_view_vertices[SM64_MARIO_VERTEX_COUNT];
static int16_vec2_t mario_screen_vertices[SM64_MARIO_VERTEX_COUNT];
static sm64_saturn_render_item_t render_items[DRAW_ITEM_COUNT];
static uint16_t render_order[DRAW_ITEM_COUNT];
static sm64_saturn_render_queue_t render_queue;
static int16_t scene_bucket_head[SCENE_DEPTH_BUCKETS], scene_bucket_tail[SCENE_DEPTH_BUCKETS];
static int16_t scene_bucket_next[DRAW_ITEM_COUNT];
static uint16_t scene_order_scratch[DRAW_ITEM_COUNT];
static uint16_t rejected_items, culled_items, animation_frame;
static sm64_saturn_frame_profile_t frame_profile;
/* Cache only the full-width source depth key.  Projection remains on the
 * proven direct path; this removes the redundant transform pass used by the
 * painter re-bucket without risking quantization of visible coordinates. */
static int32_t castle_tile_depth[SM64_CASTLE_UV_TILE_COUNT];
static uint8_t castle_tile_depth_valid[SM64_CASTLE_UV_TILE_COUNT];
static uint16_t castle_tile_depth_evaluations;
static bool mario_gouraud_dirty;
static const int16_t (*mario_frame_vertices)[3];
static angle_t mario_yaw = SM64_CASTLE_SPAWN_YAW;
static fix16_t mario_sine, mario_cosine;
static bool mario_walking;
static sm64_saturn_camera_transform_t camera_transform;
static sm64_saturn_castle_graph_state_t source_graph;
static OSContPad source_pad;
static struct Controller source_controller;
static struct MarioState source_mario_state;
static struct Area source_area;
static struct Camera source_camera;
static int32_t mario_world_x = SM64_CASTLE_SPAWN_X;
static int32_t mario_world_y = SM64_CASTLE_SPAWN_Y;
static int32_t mario_world_z = SM64_CASTLE_SPAWN_Z;
static f32 mario_vertical_velocity;
static bool mario_airborne;
static bool camera_position_initialized;
static bool controls_ready;
static sm64_saturn_cart_bank_t cartridge_bank;
static bool cartridge_present;
static uint8_t cartridge_stage[CART_STAGE_CHUNK] __aligned(32);
static uint32_t cartridge_stage_ticks;
static uint32_t cartridge_staged_bytes;

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

static void update_source_camera(void) {
    const point3_t mario = {mario_world_x, mario_world_y, mario_world_z};
    const int32_t candidate_x = SM64_CASTLE_CAMERA_BASE_X +
        (((mario.x - SM64_CASTLE_CAMERA_BASE_X) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    const int32_t candidate_z = SM64_CASTLE_CAMERA_BASE_Z +
        (((mario.z - SM64_CASTLE_CAMERA_BASE_Z) * SM64_CASTLE_CAMERA_FOLLOW_Q16) >> 16);
    /* The original camera is allowed to move in world space, but its target
     * must remain associated with a loaded Castle surface for this Saturn
     * slice. Reuse SM64's floor query to reject a camera target in empty void;
     * no room-specific rectangle or hand-authored clamp is introduced. */
    const f32 candidate_floor = find_floor_height((f32)candidate_x,
                                                   (f32)mario.y + 400.0f,
                                                   (f32)candidate_z);
    if (!camera_position_initialized ||
        (candidate_floor > FLOOR_LOWER_LIMIT &&
         candidate_floor >= (f32)mario.y - 512.0f)) {
        camera_transform.position.x = candidate_x;
        camera_transform.position.z = candidate_z;
        camera_position_initialized = true;
    }
    const f32 mario_floor = find_floor_height((f32)mario.x,
                                               (f32)mario.y + 200.0f,
                                               (f32)mario.z);
    if (mario_floor > FLOOR_LOWER_LIMIT)
        camera_transform.position.y = (int32_t)mario_floor + SM64_CASTLE_CAMERA_BASE_Y;
    else
        camera_transform.position.y = SM64_CASTLE_SPAWN_FLOOR_Y + SM64_CASTLE_CAMERA_BASE_Y;
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
    /* update_fixed_camera() limits the camera-to-focus distance to 1000
     * source units in the lobby. Keep that source rule before constructing
     * the Saturn view basis; without it, the fixed base leaves Mario too small
     * and exposes the void at the bottom of the viewport. */
    const f32 focus_dx = (f32)focus.x - camera_transform.position.x;
    const f32 focus_dy = (f32)focus.y - camera_transform.position.y;
    const f32 focus_dz = (f32)focus.z - camera_transform.position.z;
    const f32 focus_distance = sqrtf(focus_dx * focus_dx +
                                     focus_dy * focus_dy + focus_dz * focus_dz);
    if (focus_distance > 1000.0f) {
        const f32 scale = 1000.0f / focus_distance;
        camera_transform.position.x = focus.x - (int32_t)(focus_dx * scale);
        camera_transform.position.y = focus.y - (int32_t)(focus_dy * scale);
        camera_transform.position.z = focus.z - (int32_t)(focus_dz * scale);
    }
    camera_transform.forward = sm64_saturn_vec3_normalize_q16((point3_t){
        focus.x - camera_transform.position.x,
        focus.y - camera_transform.position.y,
        focus.z - camera_transform.position.z});
    camera_transform.right = sm64_saturn_vec3_normalize_q16((point3_t){
        -camera_transform.forward.z, 0, camera_transform.forward.x});
    camera_transform.up = (point3_t){
        (int32_t)((-(int64_t)camera_transform.right.z * camera_transform.forward.y) >> 16),
        (int32_t)((((int64_t)camera_transform.right.z * camera_transform.forward.x) -
                   ((int64_t)camera_transform.right.x * camera_transform.forward.z)) >> 16),
        (int32_t)(((int64_t)camera_transform.right.x * camera_transform.forward.y) >> 16)
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
    return sm64_saturn_world_to_view(&camera_transform,
                                      (point3_t){x, y, z});
#endif
}
static point3_t painter_camera_position(void) {
#if defined(SM64_SATURN_TEXTURE_PROBE_CAMERA) || defined(SM64_SATURN_TEXTURE_PROBE_PAINTER)
    return (point3_t){-1050, 720, -4200};
#else
    return camera_transform.position;
#endif
}
static point3_t castle_point(const int16_t *source) {
    return world_to_view(source[0], source[1], source[2]);
}
static const int16_t *mario_vertex(uint16_t index) {
    return mario_frame_vertices[index];
}
static int16_vec2_t project_point(point3_t point);
static void select_mario_animation_frame(void) {
    if (mario_walking)
        mario_frame_vertices = sm64_mario_walking_animation_vertices[animation_frame];
    else
        mario_frame_vertices = sm64_mario_animation_vertices[animation_frame];
}
static point3_t mario_point(const int16_t *source) {
    const int32_t x = (((int32_t)source[0] * mario_cosine) + ((int32_t)source[2] * mario_sine)) >> 16;
    const int32_t z = ((-(int32_t)source[0] * mario_sine) + ((int32_t)source[2] * mario_cosine)) >> 16;
    return world_to_view(x + mario_world_x,
                         (int32_t)source[1] + mario_world_y,
                         z + mario_world_z);
}

static void cache_mario_vertices(void) {
    for (uint16_t vertex = 0; vertex < SM64_MARIO_VERTEX_COUNT; vertex++) {
        mario_view_vertices[vertex] = mario_point(mario_vertex(vertex));
        mario_screen_vertices[vertex] = project_point(mario_view_vertices[vertex]);
    }
}

static void update_source_input(void) {
    controller_saturn.read(&source_pad);
    if (!controls_ready && source_pad.errnum == 0) {
        /* Do not turn the BIOS language/clock handoff into a gameplay edge.
         * Establish the first valid Saturn report as the held baseline; later
         * A presses still arrive through the original ControllerAPI edge. */
        source_controller.buttonDown = source_pad.button;
        controls_ready = true;
    }
    source_controller.rawStickX = source_pad.stick_x;
    source_controller.rawStickY = source_pad.stick_y;
    source_controller.buttonPressed = source_pad.button &
        (source_pad.button ^ source_controller.buttonDown);
    source_controller.buttonDown = source_pad.button;
    adjust_analog_stick(&source_controller);
    source_mario_state.input = 0;
    update_mario_button_inputs(&source_mario_state);
    update_mario_joystick_inputs(&source_mario_state);
    if (!mario_airborne && (source_controller.buttonPressed & A_BUTTON) != 0U) {
        /* Use SM64's ordinary jump impulse while the Saturn bridge is still
         * outside the full action state machine. Floor acceptance remains the
         * original collision query below, so this is not a bespoke room jump. */
        mario_vertical_velocity = 52.0f;
        mario_airborne = true;
    }
    if (mario_airborne) {
        mario_vertical_velocity -= 4.0f;
        mario_world_y += (int32_t)mario_vertical_velocity;
        const f32 floor = find_floor_height((f32)mario_world_x,
                                            (f32)mario_world_y + 200.0f,
                                            (f32)mario_world_z);
        if (floor > FLOOR_LOWER_LIMIT && floor >= (f32)mario_world_y - 256.0f &&
            (f32)mario_world_y <= floor) {
            mario_world_y = (int32_t)floor;
            mario_vertical_velocity = 0.0f;
            mario_airborne = false;
        }
    }
    mario_walking = (source_mario_state.input & INPUT_NONZERO_ANALOG) != 0U;
    /* Keep SM64's camera-relative stick semantics.  The old fixed zero yaw
     * made the Saturn pad feel like a tank: up/left were interpreted in the
     * room's world axes instead of the view axes. */
    source_camera.yaw = atan2s(-camera_transform.forward.z,
                               camera_transform.forward.x);
    if (mario_walking) {
        mario_yaw = (angle_t)source_mario_state.intendedYaw;
        fix16_sincos(mario_yaw, &mario_sine, &mario_cosine);
        /* The original SM64 joystick routine owns magnitude and intended
         * direction. This bridge advances the source actor in source units
         * until collision/action execution is linked into this target. */
        const f32 speed = (f32)source_mario_state.intendedMag / 4.0f;
        f32 next_x = (f32)mario_world_x + ((f32)mario_sine * speed) / 65536.0f;
        f32 next_z = (f32)mario_world_z + ((f32)mario_cosine * speed) / 65536.0f;
        f32 next_y = (f32)mario_world_y;
        /* Reuse SM64's spatial-partition queries over the original Castle
         * collision stream.  The Saturn renderer owns only the movement
         * bridge; floor and wall acceptance remain source engine behavior. */
        (void)f32_find_wall_collision(&next_x, &next_y, &next_z, 60.0f, 50.0f);
        const f32 floor = find_floor_height(next_x, next_y + 200.0f, next_z);
        /* A source collision query returning FLOOR_LOWER_LIMIT means the
         * candidate left the loaded Castle surface bank. Do not accept that
         * step: otherwise the follow camera chases Mario into empty space and
         * the VDP1 scene correctly exposes the void as blue/black patches. */
        /* Do not snap to a distant lower collision layer when the candidate
         * crosses a doorway edge. The source engine treats that as an invalid
         * step/OOB transition; accepting it here is what makes the follow
         * camera expose the blue/black void. */
        const bool floor_is_reachable = floor > FLOOR_LOWER_LIMIT &&
                                        floor >= next_y - 256.0f;
        if (floor_is_reachable) {
            if (!mario_airborne) next_y = floor;
            mario_world_x = (int32_t)next_x;
            mario_world_y = (int32_t)next_y;
            mario_world_z = (int32_t)next_z;
        }
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

/* For positive view depth this dot-product sign is the inverse of
 * gfx_sp_tri1()'s perspective-divided cross product. Exact BSP fragments can
 * have different winding, so retain tile-authoritative testing while avoiding
 * the more expensive homogeneous 64-bit product chain. */
static bool tile_is_culled(uint16_t primitive, point3_t a, point3_t b, point3_t c) {
    const int32_t ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const int32_t vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const int32_t nx = uy * vz - uz * vy;
    const int32_t ny = uz * vx - ux * vz;
    const int32_t nz = ux * vy - uy * vx;
    const int64_t facing = ((int64_t)nx * a.x) +
                           ((int64_t)ny * a.y) +
                           ((int64_t)nz * a.z);
    return (sm64_castle_area1_primitive_cull_back[primitive] && facing <= 0) ||
           (sm64_castle_area1_primitive_cull_front[primitive] && facing >= 0);
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
    mario_gouraud_dirty = true;
}

static void mario_texture_tile_vertices(uint16_t tile, int16_vec2_t output[4]) {
    const uint16_t *indices = sm64_mario_textured_source_vertices[
        tile / SM64_MARIO_TEXTURE_TILES_PER_SOURCE];
    const point3_t a = mario_view_vertices[indices[0]];
    const point3_t b = mario_view_vertices[indices[1]];
    const point3_t c = mario_view_vertices[indices[2]];
    const point3_t ab = {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2};
    const point3_t bc = {(b.x + c.x) / 2, (b.y + c.y) / 2, (b.z + c.z) / 2};
    const point3_t ca = {(c.x + a.x) / 2, (c.y + a.y) / 2, (c.z + a.z) / 2};
    point3_t points[3];
    if (SM64_MARIO_TEXTURE_TILES_PER_SOURCE == 1U) {
        points[0] = a; points[1] = b; points[2] = c;
    } else switch (tile & 3U) {
        case 0: points[0] = a;  points[1] = ab; points[2] = ca; break;
        case 1: points[0] = ab; points[1] = b;  points[2] = bc; break;
        case 2: points[0] = ca; points[1] = bc; points[2] = c;  break;
        default: points[0] = ab; points[1] = bc; points[2] = ca; break;
    }
    output[0] = project_point(points[0]);
    output[1] = project_point(points[1]);
    output[2] = project_point(points[2]);
    output[3] = output[2];
}

static bool tile_is_visible(uint16_t tile) {
    const uint16_t primitive = sm64_castle_uv_tile_primitive[tile];
    const uint8_t root = sm64_castle_area1_primitive_root[primitive];
    if ((source_graph.selected_root_mask & (1U << root)) == 0U) return false;
    const point3_t a = castle_point(sm64_castle_uv_positions[tile][0]);
    const point3_t b = castle_point(sm64_castle_uv_positions[tile][1]);
    const point3_t c = castle_point(sm64_castle_uv_positions[tile][2]);
    const point3_t d = sm64_castle_uv_tile_is_triangle[tile]
        ? c : castle_point(sm64_castle_uv_positions[tile][3]);
    const int32_t minimum = min4(a.z, b.z, c.z, d.z);
    const int32_t maximum = max4(a.z, b.z, c.z, d.z);
    if (minimum >= NEAR_DEPTH && tile_is_culled(primitive, a, b, c)) {
        culled_items++;
        return false;
    }
    const bool visible = minimum >= NEAR_DEPTH && maximum <= FAR_DEPTH &&
                         quad_intersects_viewport(a, b, c, d);
    if (visible) {
        castle_tile_depth[tile] = maximum;
        castle_tile_depth_valid[tile] = 1;
        castle_tile_depth_evaluations++;
    }
    return visible;
}

static void append_static_tile(uint16_t tile, sm64_saturn_render_pass_t pass) {
    if (!tile_is_visible(tile)) {
        rejected_items++;
        return;
    }
    if (!sm64_saturn_render_queue_push(&render_queue,
            (sm64_saturn_render_item_t){
                .depth_key = castle_tile_depth[tile],
                .source_bank = SOURCE_BANK_CASTLE_AREA1,
                .source_primitive = sm64_castle_uv_tile_primitive[tile],
                .lowered_index = tile,
                .kind = SM64_SATURN_RENDER_WORLD,
                .pass = pass
            })) {
        rejected_items++;
    }
}

static void sort_mario(void) {
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKETS; bucket++)
        mario_bucket_head[bucket] = mario_bucket_tail[bucket] = -1;
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const point3_t a = mario_view_vertices[indices[1]];
        const point3_t b = mario_view_vertices[indices[2]];
        const point3_t c = mario_view_vertices[indices[3]];
        const point3_t d = mario_view_vertices[indices[4]];
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
    for (uint16_t index = 0; index < mario_visible; index++) {
        const uint16_t primitive = mario_order[index];
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const int32_t maximum = max4(
            mario_view_vertices[indices[1]].z,
            mario_view_vertices[indices[2]].z,
            mario_view_vertices[indices[3]].z,
            mario_view_vertices[indices[4]].z);
        if (!sm64_saturn_render_queue_push(&render_queue,
                (sm64_saturn_render_item_t){
                    .depth_key = maximum,
                    .source_bank = SOURCE_BANK_MARIO,
                    .source_primitive = primitive,
                    .lowered_index = primitive,
                    .kind = SM64_SATURN_RENDER_ACTOR,
                    .pass = SM64_SATURN_PASS_OPAQUE
                })) {
            rejected_items++;
        }
    }
}

/* VDP1 has no depth buffer.  The source BSP is still the authority for
 * visibility, but a whole BSP leaf can contain a wall, floor, and Mario
 * whose projected depths interleave.  Re-bucket only the opaque domain with
 * a coarse, stable transformed-depth key; source traversal remains the
 * tie-breaker inside each bucket and decals stay in their explicit late pass.
 */
static void refine_opaque_depth_order(uint16_t opaque_count) {
    (void)memcpy(scene_order_scratch, render_queue.order,
                 sizeof(uint16_t) * opaque_count);
    for (uint16_t bucket = 0; bucket < SCENE_DEPTH_BUCKETS; bucket++)
        scene_bucket_head[bucket] = scene_bucket_tail[bucket] = -1;
    for (uint16_t order = 0; order < opaque_count; order++) {
        const uint16_t slot = scene_order_scratch[order];
        const int32_t depth = clamp32(render_queue.items[slot].depth_key,
                                      NEAR_DEPTH, FAR_DEPTH);
        const uint16_t bucket = (uint16_t)((depth - NEAR_DEPTH) *
            (SCENE_DEPTH_BUCKETS - 1U) / (FAR_DEPTH - NEAR_DEPTH));
        scene_bucket_next[order] = -1;
        if (scene_bucket_head[bucket] < 0)
            scene_bucket_head[bucket] = scene_bucket_tail[bucket] = (int16_t)order;
        else {
            scene_bucket_next[scene_bucket_tail[bucket]] = (int16_t)order;
            scene_bucket_tail[bucket] = (int16_t)order;
        }
    }
    uint16_t output = 0;
    for (int16_t bucket = SCENE_DEPTH_BUCKETS - 1; bucket >= 0; bucket--)
        for (int16_t order = scene_bucket_head[bucket]; order >= 0;
             order = scene_bucket_next[order])
            render_queue.order[output++] = scene_order_scratch[(uint16_t)order];
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
        append_static_tile(sm64_castle_bsp_tile_range[node][0] + offset,
                           SM64_SATURN_PASS_OPAQUE);
    traverse_bsp(near, insert_mario && !mario_in_far);
}

static void sort_scene(void) {
    rejected_items = 0;
    culled_items = 0;
    sm64_saturn_render_queue_reset(&render_queue);
    castle_tile_depth_evaluations = 0;
    (void)memset(castle_tile_depth_valid, 0, sizeof(castle_tile_depth_valid));
    sort_mario();
    traverse_bsp(0, true);
    const uint16_t opaque_count = render_queue.count;
    refine_opaque_depth_order(opaque_count);
    render_queue.opaque_count = opaque_count;
    /* True translucent decals remain a deliberately late source-derived pass. */
    for (uint16_t tile = SM64_CASTLE_BSP_DECAL_START;
         tile < SM64_CASTLE_BSP_DECAL_START + SM64_CASTLE_BSP_DECAL_COUNT; tile++)
        append_static_tile(tile, SM64_SATURN_PASS_TRANSLUCENT);
}

static void draw_castle(uint16_t tile, const vdp1_vram_partitions_t *partitions) {
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
        sm64_castle_uv_tile_is_triangle[tile]
            ? project_point(castle_point(sm64_castle_uv_positions[tile][2]))
            : project_point(castle_point(sm64_castle_uv_positions[tile][3]))
    };
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(&vdp1_backend, 1);
    if (cmdt == NULL) return;
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
}

static void draw_mario(uint16_t primitive, const vdp1_vram_partitions_t *partitions) {
    const uint16_t texture_start = sm64_mario_texture_tile_start[primitive];
    if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE) {
        const uint16_t first_tile = (texture_start / 4U) *
            SM64_MARIO_TEXTURE_TILES_PER_SOURCE;
        vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(
            &vdp1_backend, SM64_MARIO_TEXTURE_TILES_PER_SOURCE);
        if (cmdt == NULL) return;
        for (uint16_t tile = first_tile;
             tile < first_tile + SM64_MARIO_TEXTURE_TILES_PER_SOURCE; tile++) {
            int16_vec2_t vertices[4]; mario_texture_tile_vertices(tile, vertices);
            vdp1_cmdt_distorted_sprite_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){.color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)partitions->texture_base + sizeof(sm64_castle_uv_tiles) + tile * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t));
            vdp1_cmdt_char_size_set(cmdt, SM64_MARIO_TEXTURE_UV_TILE_WIDTH, SM64_MARIO_TEXTURE_UV_TILE_WIDTH);
            vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
            vdp1_cmdt_vtx_set(cmdt, vertices);
            vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions->gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
            cmdt++;
        }
        return;
    }
    const uint16_t *indices = sm64_mario_primitives[primitive];
    const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
    const int16_vec2_t vertices[4] = {
        mario_screen_vertices[indices[1]],
        mario_screen_vertices[indices[2]],
        mario_screen_vertices[indices[3]],
        mario_screen_vertices[indices[4]]
    };
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(&vdp1_backend, 1);
    if (cmdt == NULL) return;
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){.color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
    vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
    vdp1_cmdt_vtx_set(cmdt, vertices);
    vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions->gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
}

static void draw_scene(void) {
    vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
    /* The command array is initialized once at allocation.  Preserve it like
     * the intro renderer: clear only the prior END marker, overwrite the live
     * commands, then DMA only the used prefix instead of the maximum list. */
    sm64_saturn_vdp1_backend_begin(&vdp1_backend);
    /* N64 LAYER_ALPHA is binary cutout geometry that relies on the RDP's
     * Z-buffer. VDP1 has no Z-buffer, so opaque, alpha-test, and Mario must
     * share one far-to-near ordering pass. Only the genuinely translucent
     * decal root is submitted afterward with half-transparency. */
    for (uint16_t output = 0; output < render_queue.count; output++) {
        const sm64_saturn_render_item_t *item =
            &render_queue.items[render_queue.order[output]];
        if (item->kind == SM64_SATURN_RENDER_WORLD)
            draw_castle(item->lowered_index, &partitions);
        else if (item->kind == SM64_SATURN_RENDER_ACTOR)
            draw_mario(item->lowered_index, &partitions);
    }
    sm64_saturn_vdp1_backend_finish(&vdp1_backend);
    /* Gouraud tables live in VDP1 VRAM. They only change when the source
     * animation frame changes; re-uploading the whole Mario bank every frame
     * was a measurable Saturn bandwidth tax. */
    if (mario_gouraud_dirty) {
        scu_dma_transfer(0, (void *)partitions.gouraud_base, mario_gouraud, sizeof(mario_gouraud));
        scu_dma_transfer_wait(0);
        mario_gouraud_dirty = false;
    }
    sm64_saturn_vdp1_backend_upload(&vdp1_backend);
    frame_profile.command_ticks = cpu_frt_count_get();
    cpu_frt_count_set(0);
    vdp1_sync_render(); vdp1_sync(); vdp2_sync(); vdp2_sync_wait(); vdp1_sync_wait();
    frame_profile.wait_ticks = cpu_frt_count_get();
}

static void vblank_out_handler(void *work __unused) {
    smpc_peripheral_intback_issue();
}

static void
upload_texture_bank(void)
{
    const size_t castle_bytes = sizeof(sm64_castle_uv_tiles);
    const size_t mario_bytes = sizeof(sm64_mario_texture_uv_tiles);
    const size_t total_bytes = castle_bytes + mario_bytes;

    if (!cartridge_present) {
        cartridge_stage_ticks = 0;
        cartridge_staged_bytes = 0;
        if (!sm64_saturn_texture_residency_upload(
                &texture_residency, 0, sm64_castle_uv_tiles, castle_bytes) ||
            !sm64_saturn_texture_residency_upload(
                &texture_residency, castle_bytes,
                sm64_mario_texture_uv_tiles, mario_bytes)) for (;;) {}
        return;
    }

    /* Cold source textures take the cartridge path only once at startup. The
     * ring below is internal WRAM, so VDP1 never follows a slow cart pointer. */
    cpu_frt_count_set(0);
    if (!sm64_saturn_cart_bank_stage(&cartridge_bank, 0,
                                     sm64_castle_uv_tiles, castle_bytes) ||
        !sm64_saturn_cart_bank_stage(&cartridge_bank, castle_bytes,
                                     sm64_mario_texture_uv_tiles, mario_bytes)) {
        cartridge_present = false;
        cartridge_stage_ticks = 0;
        cartridge_staged_bytes = 0;
        upload_texture_bank();
        return;
    }
    cartridge_staged_bytes = (uint32_t)total_bytes;
    for (size_t offset = 0; offset < total_bytes; offset += CART_STAGE_CHUNK) {
        const size_t bytes = (total_bytes - offset) < CART_STAGE_CHUNK
            ? (total_bytes - offset) : CART_STAGE_CHUNK;
        if (!sm64_saturn_cart_bank_read(&cartridge_bank, offset,
                                        cartridge_stage, bytes)) {
            cartridge_present = false;
            cartridge_stage_ticks = 0;
            cartridge_staged_bytes = 0;
            upload_texture_bank();
            return;
        }
        if (!sm64_saturn_texture_residency_upload(
                &texture_residency, offset, cartridge_stage, bytes)) for (;;) {}
    }
    cartridge_stage_ticks = cpu_frt_count_get();
}

void user_init(void) {
    cartridge_present = sm64_saturn_cart_bank_init(&cartridge_bank);
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 4, 12));
    vdp1_env_t env; vdp1_env_default_init(&env); env.erase_color = RGB1555(1, 2, 4, 12); vdp1_env_set(&env);
    /* VDP1 remains above the diagnostic NBG3 layer so the debug console's
     * cleared cells never occlude source Castle pixels. */
    for (uint8_t priority = 0; priority < 8; priority++) vdp2_sprite_priority_set(priority, 7);
    vdp2_tvmd_display_set(); dbgio_init(); dbgio_dev_default_init(DBGIO_DEV_VDP2); dbgio_dev_font_load(); dbgio_display_set(true);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG3, 7);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    vdp_sync_vblank_out_set(vblank_out_handler, NULL);
    /* Prime the first INTBACK collection. The callback supplies subsequent
     * frames, but a target that starts polling before the first VBlank can
     * otherwise retain an all-zero, disconnected OSContPad sample. */
    smpc_peripheral_intback_issue();
    source_graph = sm64_saturn_castle_graph_init();
    if (!source_graph.valid) for (;;) {}
    alloc_surface_pools();
    load_area_terrain(1, (s16 *)sm64_castle_collision_data, NULL, NULL);
    vdp1_vram_partitions_set(COMMAND_COUNT,
        sizeof(sm64_castle_uv_tiles) + sizeof(sm64_mario_texture_uv_tiles),
        SM64_MARIO_PRIMITIVE_COUNT, SM64_CASTLE_UV_CLUT_COUNT);
    {
        const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init(
                &vdp1_backend, COMMAND_COUNT, clip, local)) for (;;) {}
    }
    sm64_saturn_render_queue_init(&render_queue, render_items, render_order,
                                  DRAW_ITEM_COUNT);
    source_area.camera = &source_camera;
    source_mario_state.area = &source_area;
    source_mario_state.controller = &source_controller;
    source_mario_state.framesSinceA = 0xFF;
    source_mario_state.framesSinceB = 0xFF;
    source_mario_state.faceAngle[1] = mario_yaw;
    source_camera.yaw = 0;
    fix16_sincos(mario_yaw, &mario_sine, &mario_cosine);
    update_source_camera();
    select_mario_animation_frame();
    build_mario_gouraud();
    {
        vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
        if (sizeof(sm64_castle_uv_tiles) + sizeof(sm64_mario_texture_uv_tiles) > partitions.texture_size) for (;;) {}
        sm64_saturn_texture_residency_init(&texture_residency, &partitions);
#if SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
        if (sizeof(sm64_castle_uv_cluts) > partitions.clut_size) for (;;) {}
        scu_dma_transfer(0, partitions.clut_base, sm64_castle_uv_cluts,
                         sizeof(sm64_castle_uv_cluts));
        scu_dma_transfer_wait(0);
#endif
        upload_texture_bank();
    }
    /* A 16-bit /128 FRT spans ~312 ms: enough to measure the current ~8 FPS
     * prototype and the 15-20 FPS acceptance band without overflow. */
    cpu_frt_init(CPU_FRT_CLOCK_DIV_128);
    for (uint32_t frame = 0;; frame++) {
        cpu_frt_count_set(0);
        update_source_input();
        const uint16_t animation_count = mario_walking
            ? SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT
            : SM64_MARIO_ANIMATION_FRAME_COUNT;
        const uint16_t animation_divisor = mario_walking ? 1U : 2U;
        const uint16_t next_frame = (uint16_t)((frame / animation_divisor) % animation_count);
        const bool animation_changed = next_frame != animation_frame;
        animation_frame = next_frame;
        select_mario_animation_frame();
        cache_mario_vertices();
        if (animation_changed) {
            /* Gouraud tables are a 28 KiB VDP1 bank for the full source actor.
             * Keep geometry animation at source cadence but update lighting at
             * a bounded Saturn-friendly cadence; this halves SCU traffic while
             * preserving the source mesh and visible material gradients. */
            if ((frame % GOURAUD_UPDATE_PERIOD) == 0U) build_mario_gouraud();
        }
        frame_profile.update_ticks = cpu_frt_count_get();
        cpu_frt_count_set(0); sort_scene(); frame_profile.sort_ticks = cpu_frt_count_get();
        cpu_frt_count_set(0); draw_scene();
        sm64_saturn_frame_profile_render_total(&frame_profile);
        if ((frame % FRAME_STATS_PERIOD) == 0) {
            const uint32_t fps_x10 = sm64_saturn_frame_profile_rate_x10(
                FRT_TICKS_PER_SECOND_X10, frame_profile.render_ticks);
            const uint32_t loop_fps_x10 = sm64_saturn_frame_profile_rate_x10(
                FRT_TICKS_PER_SECOND_X10, frame_profile.loop_ticks);
            dbgio_printf("\x1B[HSM64 SATURN M4 — SOURCE MARIO IN CASTLE\ngraph %u lists: O%u A%u D%u roots %02X | source pos %d,%d,%d\nanim %s %u/%u | input 0x%08X | painter %u/%u | reject %u cull %u\nVDP1 cmd %u/%u%s quads %u | costs U%u S%u C%u W%u V%u | cart %s %lu KiB/%lu B | render %u.%u / loop %u.%u FPS\n",
                source_graph.display_lists, source_graph.opaque_lists,
                source_graph.alpha_lists, source_graph.decal_lists,
                source_graph.selected_root_mask,
                mario_world_x, mario_world_y, mario_world_z,
                mario_walking ? "walk" : "idle", animation_frame, animation_count,
                source_mario_state.input, render_queue.count,
                (uint16_t)DRAW_ITEM_COUNT,
                rejected_items, culled_items,
                vdp1_backend.commands.live_count, vdp1_backend.commands.peak,
                vdp1_backend.commands.overflowed ? "!" : "",
                (uint16_t)SM64_CASTLE_UV_PAIRED_QUAD_COUNT,
                frame_profile.update_ticks, frame_profile.sort_ticks,
                frame_profile.command_ticks, frame_profile.wait_ticks,
                frame_profile.vblank_ticks,
                cartridge_present ? "4M" : "WRAM",
                (uint32_t)(cartridge_bank.capacity / 1024U),
                cartridge_staged_bytes,
                fps_x10 / 10U, fps_x10 % 10U, loop_fps_x10 / 10U, loop_fps_x10 % 10U);
            dbgio_flush();
        }
        cpu_frt_count_set(0);
        vdp2_tvmd_vblank_in_wait(); vdp2_tvmd_vblank_out_wait();
        frame_profile.vblank_ticks = cpu_frt_count_get();
        sm64_saturn_frame_profile_loop_total(&frame_profile);
    }
}
int main(void) { user_init(); return 0; }
