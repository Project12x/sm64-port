/* M4 integration: actual SM64 Castle Area 1 plus source Mario actor/animation. */
#include <yaul.h>
#include <string.h>
#include "sm64.h"
#include "controller_saturn.h"
#include "saturn_cart_bank.h"
#include "saturn_frame_profile.h"
#include "saturn_frame_sample.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_ir_transform.h"
#include "saturn_ir_texture.h"
#include "saturn_gouraud.h"
#include "saturn_projected_workarea.h"
#include "saturn_render_queue.h"
#include "saturn_scene_profile.h"
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

#define COMMAND_COUNT (SM64_CASTLE_UV_TILE_COUNT + SM64_MARIO_PRIMITIVE_COUNT + SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT + 3U)
#define DRAW_ITEM_COUNT (SM64_CASTLE_UV_TILE_COUNT + SM64_MARIO_PRIMITIVE_COUNT)
#define DEPTH_BUCKETS 128U
#define MARIO_BSP_EDGE_COUNT (SM64_CASTLE_BSP_NODE_COUNT * 2U)
#define NEAR_DEPTH 128
#define FAR_DEPTH 8192
#define VDP1_COORD_MIN (-1024)
#define VDP1_COORD_MAX 1023
/* A single VDP1 distorted sprite is not a safe representation for a source
 * polygon whose projected extent is much larger than the target viewport.
 * Keep the source tile/UV data intact, but reject that primitive until the
 * host lowering can split it with interpolated attributes. */
#define MAX_PROJECTED_SPAN 640
#define FRAME_STATS_PERIOD 30U
#define CART_STAGE_CHUNK 8192U
/* NTSC 320-wide master SH-2 clock (26.8224 MHz), FRT /128, times ten for
 * the one-decimal FPS display.  The former /8 timer wrapped every ~19.5 ms
 * and therefore reported nonsense for the observed ~125 ms Castle frame. */
#define FRT_TICKS_PER_SECOND_X10 2095500UL
#define SOURCE_BANK_CASTLE_AREA1 1U
#define SOURCE_BANK_MARIO 2U
#define STATIC_STREAM_CAPACITY (SM64_CASTLE_UV_TILE_COUNT + MARIO_BSP_EDGE_COUNT)

typedef sm64_saturn_vec3i_t point3_t;
typedef struct static_render_token {
    uint16_t value;
    uint8_t kind;
} static_render_token_t;

#define STATIC_TOKEN_TILE 0U
#define STATIC_TOKEN_MARIO_LEAF 1U

static sm64_saturn_vdp1_backend_t vdp1_backend;
static sm64_saturn_fast3d_frontend_t source_fast3d_frontend;
static sm64_saturn_texture_residency_t texture_residency;
static vdp1_gouraud_table_t mario_gouraud[SM64_MARIO_PRIMITIVE_COUNT];
static int16_t mario_bucket_head[DEPTH_BUCKETS], mario_bucket_tail[DEPTH_BUCKETS];
static int16_t mario_bucket_next[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t mario_order[SM64_MARIO_PRIMITIVE_COUNT], mario_visible;
static int16_t mario_leaf_head[MARIO_BSP_EDGE_COUNT];
static int16_t mario_leaf_tail[MARIO_BSP_EDGE_COUNT];
static int16_t mario_leaf_next[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t mario_primitive_edge[SM64_MARIO_PRIMITIVE_COUNT];
static point3_t mario_world_vertices[SM64_MARIO_VERTEX_COUNT];
static point3_t mario_view_vertices[SM64_MARIO_VERTEX_COUNT];
static sm64_saturn_projected_vertex_t
    mario_projected_vertices[SM64_MARIO_VERTEX_COUNT];
static sm64_saturn_projected_workarea_t mario_projected_workarea;
static point3_t castle_view_vertices[SM64_CASTLE_UV_VERTEX_COUNT];
static sm64_saturn_projected_vertex_t
    castle_projected_vertices[SM64_CASTLE_UV_VERTEX_COUNT];
static sm64_saturn_projected_workarea_t castle_projected_workarea;
static const sm64_saturn_viewport_t scene_viewport = {
    .left = 0, .top = 0, .right = 319, .bottom = 223
};
static sm64_saturn_render_item_t render_items[DRAW_ITEM_COUNT];
static uint16_t render_order[DRAW_ITEM_COUNT];
static sm64_saturn_render_queue_t render_queue;
static uint16_t rejected_items, culled_items, animation_frame;
static uint16_t animation_tick;
static bool animation_clock_started, animation_was_walking;
static uint16_t mario_bsp_straddlers;
static uint16_t mario_bsp_refined_clusters, mario_bsp_primitive_tests;
static sm64_saturn_frame_profile_t frame_profile;
volatile sm64_saturn_frame_sample_t saturn_frame_sample;
static uint16_t saturn_frame_sample_sequence;
static int32_t castle_tile_depth[SM64_CASTLE_UV_TILE_COUNT];
static static_render_token_t static_render_stream[STATIC_STREAM_CAPACITY];
static uint16_t static_render_stream_count;
static uint16_t static_render_stream_rejected;
static uint16_t static_render_stream_culled;
static bool static_render_stream_valid;
static bool castle_scene_dirty = true;
static bool castle_vertex_cache_valid;
static bool mario_gouraud_dirty;
static bool mario_projection_dirty = true;
static bool mario_sort_dirty = true;
static bool mario_sort_cache_valid;
static bool render_commands_dirty = true;
static bool mario_leaf_state_valid;
static bool mario_leaf_state_changed;
static uint16_t mario_leaf_state_changed_count;
static uint16_t mario_previous_primitive_edge[SM64_MARIO_PRIMITIVE_COUNT];
static const int16_t (*mario_frame_vertices)[3];
static const uint8_t *mario_frame_light_intensity;
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

/* Volatile by design: this is the stable remote-debugger contract for the
 * scene-neutral painter profile, separate from the legacy SAT0/HWTEST block. */
volatile sm64_saturn_scene_profile_t saturn_scene_profile;

_Static_assert(SM64_CASTLE_UV_TEXTURED_PRIMITIVE_COUNT == SM64_CASTLE_AREA1_PRIMITIVE_COUNT,
               "Castle tile painter requires the complete source material bank");

static int32_t max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
static int32_t max4(int32_t a, int32_t b, int32_t c, int32_t d) { const int32_t abc = max3(a, b, c); return abc > d ? abc : d; }
static int32_t clamp32(int32_t value, int32_t minimum, int32_t maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static void update_source_camera(void) {
    const bool had_camera = camera_position_initialized;
    const sm64_saturn_camera_transform_t previous_camera = camera_transform;
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
    if (!had_camera || memcmp(&previous_camera, &camera_transform,
                              sizeof(camera_transform)) != 0) {
        castle_scene_dirty = true;
        static_render_stream_valid = false;
        castle_vertex_cache_valid = false;
        mario_projection_dirty = true;
        mario_sort_dirty = true;
        render_commands_dirty = true;
    }
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
static const int16_t *mario_vertex(uint16_t index) {
    return mario_frame_vertices[index];
}
static sm64_saturn_projected_vertex_t project_vertex(point3_t point);
static sm64_saturn_projected_vertex_t world_to_view_project(
    point3_t world, point3_t *view);
static int16_vec2_t project_point(point3_t point);
static void select_mario_animation_frame(void) {
    if (mario_walking) {
        mario_frame_vertices = sm64_mario_walking_animation_vertices[animation_frame];
        mario_frame_light_intensity =
            sm64_mario_walking_animation_light_intensity[animation_frame];
    } else {
        mario_frame_vertices = sm64_mario_animation_vertices[animation_frame];
        mario_frame_light_intensity =
            sm64_mario_animation_light_intensity[animation_frame];
    }
}
static point3_t mario_world_point(const int16_t *source) {
    const int32_t x = (((int32_t)source[0] * mario_cosine) + ((int32_t)source[2] * mario_sine)) >> 16;
    const int32_t z = ((-(int32_t)source[0] * mario_sine) + ((int32_t)source[2] * mario_cosine)) >> 16;
    return (point3_t){x + mario_world_x,
                      (int32_t)source[1] + mario_world_y,
                      z + mario_world_z};
}

static void cache_mario_vertices(void) {
    if (!mario_projection_dirty) return;
    sm64_saturn_projected_workarea_reset(&mario_projected_workarea);
    for (uint16_t vertex = 0; vertex < SM64_MARIO_VERTEX_COUNT; vertex++) {
        mario_world_vertices[vertex] = mario_world_point(mario_vertex(vertex));
        const sm64_saturn_projected_vertex_t projected =
            world_to_view_project(mario_world_vertices[vertex],
                                  &mario_view_vertices[vertex]);
        if (!sm64_saturn_projected_workarea_push(
                &mario_projected_workarea, projected, NULL)) for (;;) {}
    }
    mario_projection_dirty = false;
}

static void cache_castle_vertices(void) {
    if (castle_vertex_cache_valid && !castle_scene_dirty) return;
    sm64_saturn_projected_workarea_reset(&castle_projected_workarea);
    for (uint16_t vertex = 0; vertex < SM64_CASTLE_UV_VERTEX_COUNT; vertex++) {
        const point3_t world = {
            sm64_castle_uv_vertices[vertex][0],
            sm64_castle_uv_vertices[vertex][1],
            sm64_castle_uv_vertices[vertex][2]
        };
        const sm64_saturn_projected_vertex_t projected =
            world_to_view_project(world, &castle_view_vertices[vertex]);
        if (!sm64_saturn_projected_workarea_push(
                &castle_projected_workarea, projected, NULL)) for (;;) {}
    }
    castle_vertex_cache_valid = true;
}

static void update_source_input(void) {
    const int32_t previous_x = mario_world_x;
    const int32_t previous_y = mario_world_y;
    const int32_t previous_z = mario_world_z;
    const angle_t previous_yaw = mario_yaw;
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
    if (!camera_position_initialized ||
        previous_x != mario_world_x || previous_y != mario_world_y ||
        previous_z != mario_world_z)
        update_source_camera();
    if (previous_x != mario_world_x || previous_y != mario_world_y ||
        previous_z != mario_world_z || previous_yaw != mario_yaw) {
        mario_projection_dirty = true;
        mario_sort_dirty = true;
        render_commands_dirty = true;
    }
}

static void publish_frame_sample(void) {
    const uint16_t sequence = (uint16_t)(saturn_frame_sample_sequence + 2U);
    const uint32_t render_ticks = frame_profile.render_ticks;
    const uint32_t loop_ticks = frame_profile.loop_ticks;
    const uint32_t x = (uint32_t)mario_world_x;
    const uint32_t y = (uint32_t)mario_world_y;
    const uint32_t z = (uint32_t)mario_world_z;

    /* An odd sequence tells a remote reader not to consume a partial record. */
    saturn_frame_sample.sequence = (uint16_t)(sequence - 1U);
    saturn_frame_sample.magic = SM64_SATURN_FRAME_SAMPLE_MAGIC;
    saturn_frame_sample.version = SM64_SATURN_FRAME_SAMPLE_VERSION;
    saturn_frame_sample.size = sizeof(saturn_frame_sample);
    saturn_frame_sample.update_ticks = frame_profile.update_ticks;
    saturn_frame_sample.sort_ticks = frame_profile.sort_ticks;
    saturn_frame_sample.command_ticks = frame_profile.command_ticks;
    saturn_frame_sample.wait_ticks = frame_profile.wait_ticks;
    saturn_frame_sample.vblank_ticks = frame_profile.vblank_ticks;
    saturn_frame_sample.render_ticks_hi = (uint16_t)(render_ticks >> 16);
    saturn_frame_sample.render_ticks_lo = (uint16_t)render_ticks;
    saturn_frame_sample.loop_ticks_hi = (uint16_t)(loop_ticks >> 16);
    saturn_frame_sample.loop_ticks_lo = (uint16_t)loop_ticks;
    saturn_frame_sample.mario_walking = mario_walking ? 1U : 0U;
    saturn_frame_sample.animation_frame = animation_frame;
    saturn_frame_sample.mario_world_x_hi = (uint16_t)(x >> 16);
    saturn_frame_sample.mario_world_x_lo = (uint16_t)x;
    saturn_frame_sample.mario_world_y_hi = (uint16_t)(y >> 16);
    saturn_frame_sample.mario_world_y_lo = (uint16_t)y;
    saturn_frame_sample.mario_world_z_hi = (uint16_t)(z >> 16);
    saturn_frame_sample.mario_world_z_lo = (uint16_t)z;
    saturn_frame_sample.sequence = sequence;
    saturn_frame_sample_sequence = sequence;
}
/* Close-port of libmic3d's MIT-licensed transform-once projection schedule:
 * start one SH-2 DIVU reciprocal, prepare X/Y while it runs, then reuse that
 * quotient for both screen coordinates. No software ___sdivsi3 is required. */
static sm64_saturn_projected_vertex_t project_vertex(point3_t point) {
    const sm64_saturn_ir_transform_job_t job = {
        .camera = {
            .position = {0, 0, 0},
            .right = {1 << 16, 0, 0},
            .up = {0, 1 << 16, 0},
            .forward = {0, 0, 1 << 16}
        },
        .focal_length = SM64_CASTLE_CAMERA_FOCAL_LENGTH,
        .near_depth = NEAR_DEPTH,
        .center_x = 160,
        .center_y = 112,
        .coord_min = VDP1_COORD_MIN,
        .coord_max = VDP1_COORD_MAX
    };
    sm64_saturn_vec3i_t view;
    sm64_saturn_projected_vertex_t projected;
    if (!sm64_saturn_ir_transform_one(&job, point, &view, &projected)) {
        return (sm64_saturn_projected_vertex_t){0, 0, point.z};
    }
    return projected;
}

/* Close-port the full libmic3d scheduling pattern, not merely its reciprocal:
 * transform Z first, launch SH-2 DIVU, compute the independent X/Y camera dot
 * products while DIVU runs, then consume the quotient once. */
static sm64_saturn_projected_vertex_t world_to_view_project(
    point3_t world, point3_t *view)
{
#if defined(SM64_SATURN_TEXTURE_PROBE_CAMERA) || defined(SM64_SATURN_TEXTURE_PROBE_PROJECTION)
    *view = world_to_view(world.x, world.y, world.z);
    return project_vertex(*view);
#else
    const sm64_saturn_ir_transform_job_t job = {
        .camera = camera_transform,
        .focal_length = SM64_CASTLE_CAMERA_FOCAL_LENGTH,
        .near_depth = NEAR_DEPTH,
        .center_x = 160,
        .center_y = 112,
        .coord_min = VDP1_COORD_MIN,
        .coord_max = VDP1_COORD_MAX
    };
    sm64_saturn_projected_vertex_t projected;
    if (!sm64_saturn_ir_transform_one(&job, world, view, &projected)) {
        *view = (sm64_saturn_vec3i_t){0, 0, world.z};
        return (sm64_saturn_projected_vertex_t){0, 0, world.z};
    }
    return projected;
#endif
}

static int16_vec2_t project_point(point3_t point) {
    const sm64_saturn_projected_vertex_t projected = project_vertex(point);
    const int16_vec2_t result = INT16_VEC2_INITIALIZER(
        projected.x, projected.y);
    return result;
}

/* For positive view depth this dot-product sign is the inverse of
 * gfx_sp_tri1()'s perspective-divided cross product. Exact BSP fragments can
 * have different winding, so retain tile-authoritative testing while avoiding
 * the more expensive homogeneous 64-bit product chain. */
static int64_t view_triangle_facing(point3_t a, point3_t b, point3_t c) {
    const int32_t ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const int32_t vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const int32_t nx = uy * vz - uz * vy;
    const int32_t ny = uz * vx - ux * vz;
    const int32_t nz = ux * vy - uy * vx;
    return ((int64_t)nx * a.x) +
           ((int64_t)ny * a.y) +
           ((int64_t)nz * a.z);
}

static bool view_triangle_is_culled(point3_t a, point3_t b, point3_t c,
                                    bool cull_back, bool cull_front) {
    const int64_t facing = view_triangle_facing(a, b, c);
    return (cull_back && facing <= 0) || (cull_front && facing >= 0);
}

static bool tile_is_culled(uint16_t primitive, point3_t a, point3_t b, point3_t c) {
    return view_triangle_is_culled(
        a, b, c,
        sm64_castle_area1_primitive_cull_back[primitive],
        sm64_castle_area1_primitive_cull_front[primitive]);
}

static bool mario_primitive_is_culled(uint16_t primitive) {
    const uint16_t *indices = sm64_mario_primitives[primitive];
    return view_triangle_is_culled(
        mario_view_vertices[indices[1]],
        mario_view_vertices[indices[2]],
        mario_view_vertices[indices[3]],
        sm64_mario_primitive_cull_back[primitive], false);
}

static void build_mario_gouraud(void) {
    /* The host compiler evaluates the identical source-pose normal/light
     * expression once per animation frame. Runtime expands one compact
     * uint8 intensity per shared vertex into the VDP1 material tables: no
     * 64-bit normal accumulation and no software division in the frame loop. */
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
        for (uint8_t corner = 0; corner < 4; corner++) {
            const uint8_t intensity =
                mario_frame_light_intensity[indices[corner + 1]];
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
    if (SM64_MARIO_TEXTURE_TILES_PER_SOURCE == 1U) {
        output[0] = (int16_vec2_t)INT16_VEC2_INITIALIZER(
            mario_projected_vertices[indices[0]].x,
            mario_projected_vertices[indices[0]].y);
        output[1] = (int16_vec2_t)INT16_VEC2_INITIALIZER(
            mario_projected_vertices[indices[1]].x,
            mario_projected_vertices[indices[1]].y);
        output[2] = (int16_vec2_t)INT16_VEC2_INITIALIZER(
            mario_projected_vertices[indices[2]].x,
            mario_projected_vertices[indices[2]].y);
        output[3] = output[2];
        return;
    }
    const point3_t a = mario_view_vertices[indices[0]];
    const point3_t b = mario_view_vertices[indices[1]];
    const point3_t c = mario_view_vertices[indices[2]];
    const point3_t ab = {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2};
    const point3_t bc = {(b.x + c.x) / 2, (b.y + c.y) / 2, (b.z + c.z) / 2};
    const point3_t ca = {(c.x + a.x) / 2, (c.y + a.y) / 2, (c.z + a.z) / 2};
    point3_t points[3];
    switch (tile & 3U) {
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
    const uint16_t *indices = sm64_castle_uv_tile_vertices[tile];
    const point3_t a = castle_view_vertices[indices[0]];
    const point3_t b = castle_view_vertices[indices[1]];
    const point3_t c = castle_view_vertices[indices[2]];
    sm64_saturn_projected_quad_t quad;
    if (!sm64_saturn_projected_quad_analyze(
            &castle_projected_workarea, indices, &scene_viewport, &quad)) {
        return false;
    }
    if (quad.min_z >= NEAR_DEPTH && tile_is_culled(primitive, a, b, c)) {
        culled_items++;
        return false;
    }
    const bool visible = sm64_saturn_projected_quad_is_visible(
        &quad, NEAR_DEPTH, FAR_DEPTH, MAX_PROJECTED_SPAN);
    if (visible) {
        castle_tile_depth[tile] = quad.max_z;
    }
    return visible;
}

static void append_static_tile_to_stream(uint16_t tile) {
    if (!tile_is_visible(tile)) {
        static_render_stream_rejected++;
        return;
    }
    if (static_render_stream_count >= STATIC_STREAM_CAPACITY) {
        static_render_stream_rejected++;
        return;
    }
    static_render_stream[static_render_stream_count++] =
        (static_render_token_t){tile, STATIC_TOKEN_TILE};
}

static void append_mario_leaf_to_stream(uint16_t edge) {
    if (static_render_stream_count >= STATIC_STREAM_CAPACITY) {
        static_render_stream_rejected++;
        return;
    }
    static_render_stream[static_render_stream_count++] =
        (static_render_token_t){edge, STATIC_TOKEN_MARIO_LEAF};
}

static void push_static_tile(uint16_t tile) {
    if (!sm64_saturn_render_queue_push(&render_queue,
            (sm64_saturn_render_item_t){
                .depth_key = castle_tile_depth[tile],
                .source_bank = SOURCE_BANK_CASTLE_AREA1,
                .source_primitive = sm64_castle_uv_tile_primitive[tile],
                .lowered_index = tile,
                .kind = SM64_SATURN_RENDER_WORLD,
                .pass = sm64_castle_area1_primitive_layer[
                    sm64_castle_uv_tile_primitive[tile]] ==
                        SM64_CASTLE_LAYER_TRANSPARENT_DECAL
                    ? SM64_SATURN_PASS_DECAL
                    : SM64_SATURN_PASS_OPAQUE
            })) {
        rejected_items++;
    }
}

static int64_t bsp_side(uint16_t node, point3_t point);

/* Assign each dynamic source primitive to a static painter leaf using its
 * complete world-space bounds. A primitive that crosses a partition records
 * the fallback and follows its signed center until the global clipper can
 * split it; the actor origin is never used as a proxy for the whole mesh. */
static uint16_t mario_primitive_leaf_edge_from(
    uint16_t primitive, int16_t node)
{
    const uint16_t *indices = sm64_mario_primitives[primitive];
    bool straddled = false;
    while (node >= 0) {
        mario_bsp_primitive_tests++;
        int64_t minimum = INT64_MAX;
        int64_t maximum = INT64_MIN;
        int64_t center = 0;
        for (uint8_t corner = 1; corner < 5; corner++) {
            const int64_t side = bsp_side(
                (uint16_t)node, mario_world_vertices[indices[corner]]);
            if (side < minimum) minimum = side;
            if (side > maximum) maximum = side;
            center += side;
        }
        if (minimum < 0 && maximum > 0) straddled = true;
        const uint16_t child_index = center >= 0 ? 0U : 1U;
        const int16_t child = sm64_castle_bsp_children[node][child_index];
        if (child < 0) {
            if (straddled) mario_bsp_straddlers++;
            return (uint16_t)node * 2U + child_index;
        }
        node = child;
    }
    return 0;
}

static void set_mario_primitive_edge(uint16_t primitive, uint16_t edge) {
    if (!mario_leaf_state_valid ||
        mario_previous_primitive_edge[primitive] != edge) {
        mario_leaf_state_changed = true;
        if (mario_leaf_state_changed_count != UINT16_MAX)
            mario_leaf_state_changed_count++;
    }
    mario_primitive_edge[primitive] = edge;
}

/* Z-Treme-style leaf work units avoid descending every actor primitive
 * through the world BSP. Classify one current-pose cluster AABB; refine only
 * a cluster that actually crosses a structural plane. This is scene-neutral:
 * clusters come from originating Fast3D leaf display lists and the target BSP
 * remains an area-package input. */
static void assign_mario_cluster(uint16_t cluster) {
    const uint16_t vertex_start =
        sm64_mario_render_cluster_vertex_offsets[cluster];
    const uint16_t vertex_end =
        sm64_mario_render_cluster_vertex_offsets[cluster + 1U];
    point3_t minimum = {INT32_MAX, INT32_MAX, INT32_MAX};
    point3_t maximum = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint16_t entry = vertex_start; entry < vertex_end; entry++) {
        const point3_t point = mario_world_vertices[
            sm64_mario_render_cluster_vertex_list[entry]];
        if (point.x < minimum.x) minimum.x = point.x;
        if (point.y < minimum.y) minimum.y = point.y;
        if (point.z < minimum.z) minimum.z = point.z;
        if (point.x > maximum.x) maximum.x = point.x;
        if (point.y > maximum.y) maximum.y = point.y;
        if (point.z > maximum.z) maximum.z = point.z;
    }

    int16_t node = 0;
    while (node >= 0) {
        const int32_t *normal = sm64_castle_bsp_normal[node];
        int64_t minimum_side = sm64_castle_bsp_distance[node];
        int64_t maximum_side = sm64_castle_bsp_distance[node];
        const int32_t lower[3] = {minimum.x, minimum.y, minimum.z};
        const int32_t upper[3] = {maximum.x, maximum.y, maximum.z};
        for (uint8_t axis = 0; axis < 3; axis++) {
            if (normal[axis] >= 0) {
                minimum_side += (int64_t)normal[axis] * lower[axis];
                maximum_side += (int64_t)normal[axis] * upper[axis];
            } else {
                minimum_side += (int64_t)normal[axis] * upper[axis];
                maximum_side += (int64_t)normal[axis] * lower[axis];
            }
        }
        if (minimum_side < 0 && maximum_side > 0) {
            mario_bsp_refined_clusters++;
            const uint16_t primitive_start =
                sm64_mario_render_cluster_primitive_offsets[cluster];
            const uint16_t primitive_end =
                sm64_mario_render_cluster_primitive_offsets[cluster + 1U];
            for (uint16_t entry = primitive_start; entry < primitive_end; entry++) {
                const uint16_t primitive =
                    sm64_mario_render_cluster_primitive_list[entry];
                set_mario_primitive_edge(
                    primitive, mario_primitive_leaf_edge_from(primitive, node));
            }
            return;
        }
        const uint16_t child_index = minimum_side >= 0 ? 0U : 1U;
        const int16_t child = sm64_castle_bsp_children[node][child_index];
        if (child < 0) {
            const uint16_t edge = (uint16_t)node * 2U + child_index;
            const uint16_t primitive_start =
                sm64_mario_render_cluster_primitive_offsets[cluster];
            const uint16_t primitive_end =
                sm64_mario_render_cluster_primitive_offsets[cluster + 1U];
            for (uint16_t entry = primitive_start; entry < primitive_end; entry++)
                set_mario_primitive_edge(
                    sm64_mario_render_cluster_primitive_list[entry], edge);
            return;
        }
        node = child;
    }
}

static void sort_mario(void) {
    for (uint16_t bucket = 0; bucket < DEPTH_BUCKETS; bucket++)
        mario_bucket_head[bucket] = mario_bucket_tail[bucket] = -1;
    for (uint16_t primitive = 0; primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const uint16_t projected_indices[4] = {
            indices[1], indices[2], indices[3], indices[4]
        };
        sm64_saturn_projected_quad_t quad;
        if (!sm64_saturn_projected_quad_analyze(
                &mario_projected_workarea, projected_indices,
                &scene_viewport, &quad) ||
            !sm64_saturn_projected_quad_is_visible(
                &quad, NEAR_DEPTH, FAR_DEPTH, MAX_PROJECTED_SPAN)) {
            mario_bucket_next[primitive] = -2;
            rejected_items++;
            continue;
        }
        /* Honor the G_CULL_BACK state carried through Mario's source
         * GeoLayout. Keep the near-clip exception aligned with the static
         * stream: a triangle intersecting the near plane may change winding
         * after clipping, so the projected visibility path owns that case. */
        if (quad.min_z >= NEAR_DEPTH && mario_primitive_is_culled(primitive)) {
            mario_bucket_next[primitive] = -2;
            culled_items++;
            continue;
        }
        const uint16_t bucket = (uint16_t)((quad.max_z - NEAR_DEPTH) *
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

    for (uint16_t edge = 0; edge < MARIO_BSP_EDGE_COUNT; edge++)
        mario_leaf_head[edge] = mario_leaf_tail[edge] = -1;
    mario_bsp_straddlers = 0;
    mario_bsp_refined_clusters = 0;
    mario_bsp_primitive_tests = 0;
    mario_leaf_state_changed = false;
    mario_leaf_state_changed_count = 0;
    for (uint16_t cluster = 0; cluster < SM64_MARIO_RENDER_CLUSTER_COUNT;
         cluster++)
        assign_mario_cluster(cluster);
    for (uint16_t order = 0; order < mario_visible; order++) {
        const uint16_t primitive = mario_order[order];
        const uint16_t edge = mario_primitive_edge[primitive];
        mario_leaf_next[primitive] = -1;
        if (mario_leaf_head[edge] < 0)
            mario_leaf_head[edge] = mario_leaf_tail[edge] = (int16_t)primitive;
        else {
            mario_leaf_next[mario_leaf_tail[edge]] = (int16_t)primitive;
            mario_leaf_tail[edge] = (int16_t)primitive;
        }
    }
    for (uint16_t primitive = 0;
         primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++)
        mario_previous_primitive_edge[primitive] = mario_primitive_edge[primitive];
    mario_leaf_state_valid = true;
}

static void append_mario_leaf(uint16_t edge) {
    for (int16_t entry = mario_leaf_head[edge]; entry >= 0;
         entry = mario_leaf_next[entry]) {
        const uint16_t primitive = (uint16_t)entry;
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

static int64_t bsp_side(uint16_t node, point3_t point) {
    return ((int64_t)sm64_castle_bsp_normal[node][0] * point.x) +
           ((int64_t)sm64_castle_bsp_normal[node][1] * point.y) +
           ((int64_t)sm64_castle_bsp_normal[node][2] * point.z) +
           sm64_castle_bsp_distance[node];
}

static void traverse_bsp_stream(int16_t node);

static void traverse_bsp_stream_edge(int16_t child, uint16_t edge) {
    if (child < 0)
        append_mario_leaf_to_stream(edge);
    else
        traverse_bsp_stream(child);
}

static void traverse_bsp_stream(int16_t node) {
    const bool camera_front = bsp_side((uint16_t)node, painter_camera_position()) >= 0;
    const int16_t front = sm64_castle_bsp_children[node][0];
    const int16_t back = sm64_castle_bsp_children[node][1];
    const int16_t far = camera_front ? back : front;
    const int16_t near = camera_front ? front : back;
    const uint16_t far_edge = (uint16_t)node * 2U +
        (camera_front ? 1U : 0U);
    const uint16_t near_edge = (uint16_t)node * 2U +
        (camera_front ? 0U : 1U);
    traverse_bsp_stream_edge(far, far_edge);
    for (uint16_t offset = 0; offset < sm64_castle_bsp_tile_range[node][1]; offset++)
        append_static_tile_to_stream(sm64_castle_bsp_tile_range[node][0] + offset);
    traverse_bsp_stream_edge(near, near_edge);
}

static void emit_static_stream(void) {
    for (uint16_t index = 0; index < static_render_stream_count; index++) {
        const static_render_token_t token = static_render_stream[index];
        if (token.kind == STATIC_TOKEN_MARIO_LEAF)
            append_mario_leaf(token.value);
        else
            push_static_tile(token.value);
    }
}

static void sort_scene(void) {
    const uint16_t sort_start = cpu_frt_count_get();
    uint16_t sort_mario_ticks = 0;
    uint16_t static_rebuild_ticks = 0;
    uint16_t emit_ticks = 0;
    const bool sort_cache_hit = mario_sort_cache_valid && !mario_sort_dirty;
    if (sort_cache_hit) mario_leaf_state_changed_count = 0;
    rejected_items = 0;
    culled_items = 0;
    sm64_saturn_render_queue_reset(&render_queue);
    if (!sort_cache_hit) {
        sort_mario();
        sort_mario_ticks = (uint16_t)(cpu_frt_count_get() - sort_start);
        mario_sort_cache_valid = true;
        mario_sort_dirty = false;
        render_commands_dirty = true;
        /* Every source BSP leaf already contributes a Mario-leaf marker to the
         * cached topology.  Only the linked primitive lists change when an
         * animated cluster crosses a plane, so the global far-to-near stream
         * remains exact without rebuilding all static visibility work. */
    }
    const uint16_t static_start = cpu_frt_count_get();
    if (castle_scene_dirty || !static_render_stream_valid) {
        static_render_stream_count = 0;
        static_render_stream_rejected = 0;
        const uint16_t culled_before = culled_items;
        traverse_bsp_stream(0);
        static_render_stream_culled = culled_items - culled_before;
        static_render_stream_valid = true;
        castle_scene_dirty = false;
    } else {
        rejected_items += static_render_stream_rejected;
        culled_items += static_render_stream_culled;
    }
    static_rebuild_ticks = (uint16_t)(cpu_frt_count_get() - static_start);
    const uint16_t emit_start = cpu_frt_count_get();
    emit_static_stream();
    /* Opaque surfaces, alpha-test geometry, coplanar decals, and actors share
     * one topology-derived painter stream. VDP1 has no depth buffer, so a
     * global late decal pass would inevitably paint the lobby emblem over
     * Mario. Coplanar base-before-decal order is compiled into the BSP. */
    render_queue.opaque_count = render_queue.count;
    emit_ticks = (uint16_t)(cpu_frt_count_get() - emit_start);
    saturn_scene_profile.magic = SM64_SATURN_SCENE_PROFILE_MAGIC;
    saturn_scene_profile.version = SM64_SATURN_SCENE_PROFILE_VERSION;
    saturn_scene_profile.size = sizeof(saturn_scene_profile);
    saturn_scene_profile.sort_mario_ticks = sort_mario_ticks;
    saturn_scene_profile.static_rebuild_ticks = static_rebuild_ticks;
    saturn_scene_profile.emit_ticks = emit_ticks;
    saturn_scene_profile.sort_cache_hit = sort_cache_hit;
    saturn_scene_profile.static_cache_hit =
        !castle_scene_dirty && static_render_stream_valid &&
        static_rebuild_ticks == 0;
    saturn_scene_profile.static_stream_count = static_render_stream_count;
    saturn_scene_profile.mario_visible = mario_visible;
    saturn_scene_profile.mario_rejected = rejected_items;
    saturn_scene_profile.mario_culled = culled_items;
    saturn_scene_profile.mario_edge_changed = mario_leaf_state_changed_count;
    saturn_scene_profile.static_rejected = static_render_stream_rejected;
    saturn_scene_profile.static_culled = static_render_stream_culled;
    saturn_scene_profile.mario_bsp_tests = mario_bsp_primitive_tests;
    saturn_scene_profile.mario_bsp_refined_clusters = mario_bsp_refined_clusters;
    saturn_scene_profile.mario_bsp_straddlers = mario_bsp_straddlers;
    saturn_scene_profile.scene_dirty = castle_scene_dirty;
    saturn_scene_profile.mario_dirty = mario_sort_dirty;
    saturn_scene_profile.static_valid = static_render_stream_valid;
}

static void draw_castle(uint16_t tile, const vdp1_vram_partitions_t *partitions) {
    const uint16_t primitive = sm64_castle_uv_tile_primitive[tile];
    const uint8_t layer = sm64_castle_area1_primitive_layer[primitive];
    const vdp1_cmdt_cc_t color_calculation =
        layer == SM64_CASTLE_LAYER_TRANSPARENT_DECAL
            ? VDP1_CMDT_CC_HALF_TRANSPARENT
            : VDP1_CMDT_CC_REPLACE;
    const uint16_t *indices = sm64_castle_uv_tile_vertices[tile];
    const int16_vec2_t vertices[4] = {
        INT16_VEC2_INITIALIZER(castle_projected_vertices[indices[0]].x,
                               castle_projected_vertices[indices[0]].y),
        INT16_VEC2_INITIALIZER(castle_projected_vertices[indices[1]].x,
                               castle_projected_vertices[indices[1]].y),
        INT16_VEC2_INITIALIZER(castle_projected_vertices[indices[2]].x,
                               castle_projected_vertices[indices[2]].y),
        INT16_VEC2_INITIALIZER(castle_projected_vertices[indices[3]].x,
                               castle_projected_vertices[indices[3]].y)
    };
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(&vdp1_backend, 1);
    if (cmdt == NULL) return;
#if SM64_CASTLE_UV_TEXTURE_FORMAT_CLUT16
    (void)sm64_saturn_ir_texture_bind_clut16(
        cmdt, partitions, tile * SM64_CASTLE_UV_TILE_BYTES,
        SM64_CASTLE_UV_TILE_WIDTH, SM64_CASTLE_UV_TILE_WIDTH,
        sm64_castle_uv_tile_clut[tile], color_calculation, vertices);
#else
    (void)sm64_saturn_ir_texture_bind_rgb1555(
        cmdt, partitions, tile * SM64_CASTLE_UV_TILE_BYTES,
        SM64_CASTLE_UV_TILE_WIDTH, SM64_CASTLE_UV_TILE_WIDTH,
        color_calculation, vertices);
#endif
}

static void draw_mario(uint16_t primitive, const vdp1_vram_partitions_t *partitions) {
    const uint16_t texture_start = sm64_mario_texture_tile_start[primitive];
    if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE) {
        const uint16_t first_tile = (texture_start / 4U) *
            SM64_MARIO_TEXTURE_TILES_PER_SOURCE;
        vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(
            &vdp1_backend, 1U + SM64_MARIO_TEXTURE_TILES_PER_SOURCE);
        if (cmdt == NULL) return;
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const int16_vec2_t base_vertices[4] = {
            INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[1]].x,
                                   mario_projected_vertices[indices[1]].y),
            INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[2]].x,
                                   mario_projected_vertices[indices[2]].y),
            INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[3]].x,
                                   mario_projected_vertices[indices[3]].y),
            INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[4]].x,
                                   mario_projected_vertices[indices[4]].y)
        };
        /* G_CC_BLENDRGBFADEA is one opaque material surface plus alpha-keyed
         * raw texture detail.  Keep both commands adjacent so the render
         * queue orders one source primitive, never two scene-level passes. */
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_GOURAUD
        });
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = sm64_saturn_gouraud_neutral_color()
        });
        vdp1_cmdt_vtx_set(cmdt, base_vertices);
        vdp1_cmdt_gouraud_base_set(cmdt,
            (vdp1_vram_t)partitions->gouraud_base +
            primitive * sizeof(vdp1_gouraud_table_t));
        cmdt++;
        for (uint16_t tile = first_tile;
             tile < first_tile + SM64_MARIO_TEXTURE_TILES_PER_SOURCE; tile++) {
            int16_vec2_t vertices[4]; mario_texture_tile_vertices(tile, vertices);
            (void)sm64_saturn_ir_texture_bind_rgb1555(
                cmdt, partitions,
                sizeof(sm64_castle_uv_tiles) + tile *
                    SM64_MARIO_TEXTURE_UV_TILE_WIDTH *
                    SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t),
                SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                VDP1_CMDT_CC_REPLACE, vertices);
            cmdt++;
        }
        return;
    }
    const uint16_t *indices = sm64_mario_primitives[primitive];
    const int16_vec2_t vertices[4] = {
        INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[1]].x,
                               mario_projected_vertices[indices[1]].y),
        INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[2]].x,
                               mario_projected_vertices[indices[2]].y),
        INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[3]].x,
                               mario_projected_vertices[indices[3]].y),
        INT16_VEC2_INITIALIZER(mario_projected_vertices[indices[4]].x,
                               mario_projected_vertices[indices[4]].y)
    };
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(&vdp1_backend, 1);
    if (cmdt == NULL) return;
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){.color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
    vdp1_cmdt_color_set(cmdt, (rgb1555_t){
        .raw = sm64_saturn_gouraud_neutral_color()
    });
    vdp1_cmdt_vtx_set(cmdt, vertices);
    vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)partitions->gouraud_base + primitive * sizeof(vdp1_gouraud_table_t));
}

static void draw_scene(void) {
    vdp1_vram_partitions_t partitions; vdp1_vram_partitions_get(&partitions);
    /* The command array is initialized once at allocation.  Preserve it like
     * the intro renderer: clear only the prior END marker, overwrite the live
     * commands, then DMA only the used prefix instead of the maximum list. */
    if (render_commands_dirty) {
        sm64_saturn_vdp1_backend_begin(&vdp1_backend);
        /* N64 LAYER_ALPHA is binary cutout geometry that relies on the RDP's
         * Z-buffer. VDP1 has no Z-buffer, so opaque, alpha-test, coplanar
         * decals, and Mario must share one far-to-near topology/dependency
         * stream. */
        for (uint16_t output = 0; output < render_queue.count; output++) {
            const sm64_saturn_render_item_t *item =
                &render_queue.items[render_queue.order[output]];
            if (item->kind == SM64_SATURN_RENDER_WORLD)
                draw_castle(item->lowered_index, &partitions);
            else if (item->kind == SM64_SATURN_RENDER_ACTOR)
                draw_mario(item->lowered_index, &partitions);
        }
        sm64_saturn_vdp1_backend_finish(&vdp1_backend);
        render_commands_dirty = false;
    }
    /* Gouraud tables live in VDP1 VRAM. They only change when the source
     * animation frame changes; re-uploading the whole Mario bank every frame
     * was a measurable Saturn bandwidth tax. */
    if (mario_gouraud_dirty) {
        scu_dma_transfer(0, (void *)partitions.gouraud_base, mario_gouraud, sizeof(mario_gouraud));
        scu_dma_transfer_wait(0);
        mario_gouraud_dirty = false;
    }
    /* VDP1 still needs the persistent command prefix transferred each frame
     * to arm its render request, but the expensive command lowering is skipped
     * whenever the source pose/camera/global painter key is unchanged. */
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
    sm64_saturn_fast3d_frontend_init(&source_fast3d_frontend);
    sm64_saturn_source_runtime_configure(
        sm64_saturn_fast3d_frontend_submit, &source_fast3d_frontend);
    if (!sm64_saturn_source_runtime_preflight_task()) for (;;) {}
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
    sm64_saturn_projected_workarea_init(
        &mario_projected_workarea, mario_projected_vertices,
        SM64_MARIO_VERTEX_COUNT);
    sm64_saturn_projected_workarea_init(
        &castle_projected_workarea, castle_projected_vertices,
        SM64_CASTLE_UV_VERTEX_COUNT);
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
        bool animation_changed = false;
        if (!animation_clock_started || mario_walking != animation_was_walking) {
            animation_clock_started = true;
            animation_was_walking = mario_walking;
            animation_tick = 0;
            animation_frame = 0;
            animation_changed = true;
        } else {
            animation_tick++;
            if (animation_tick >= animation_divisor) {
                animation_tick = 0;
                animation_frame++;
                if (animation_frame >= animation_count) animation_frame = 0;
                animation_changed = true;
            }
        }
        if (animation_changed) {
            mario_projection_dirty = true;
            mario_sort_dirty = true;
            render_commands_dirty = true;
        }
        select_mario_animation_frame();
        cache_mario_vertices();
        cache_castle_vertices();
        if (animation_changed) build_mario_gouraud();
        frame_profile.update_ticks = cpu_frt_count_get();
        cpu_frt_count_set(0); sort_scene(); frame_profile.sort_ticks = cpu_frt_count_get();
        cpu_frt_count_set(0); draw_scene();
        sm64_saturn_frame_profile_render_total(&frame_profile);
        if ((frame % FRAME_STATS_PERIOD) == 0) {
            const uint32_t fps_x10 = sm64_saturn_frame_profile_rate_x10(
                FRT_TICKS_PER_SECOND_X10, frame_profile.render_ticks);
            const uint32_t loop_fps_x10 = sm64_saturn_frame_profile_rate_x10(
                FRT_TICKS_PER_SECOND_X10, frame_profile.loop_ticks);
            dbgio_printf("\x1B[HSM64 SATURN M4 — SOURCE MARIO IN CASTLE\ngraph %u lists: O%u A%u D%u roots %02X | source pos %d,%d,%d\nanim %s %u/%u | input 0x%08X | painter %u/%u | reject %u cull %u\nbsp refine %u prim %u cross %u | VDP1 cmd %u/%u%s quads %u | costs U%u S%u C%u W%u V%u | cart %s %lu KiB/%lu B | render %u.%u / loop %u.%u FPS\n",
                source_graph.display_lists, source_graph.opaque_lists,
                source_graph.alpha_lists, source_graph.decal_lists,
                source_graph.selected_root_mask,
                mario_world_x, mario_world_y, mario_world_z,
                mario_walking ? "walk" : "idle", animation_frame, animation_count,
                source_mario_state.input, render_queue.count,
                (uint16_t)DRAW_ITEM_COUNT,
                rejected_items, culled_items,
                mario_bsp_refined_clusters, mario_bsp_primitive_tests,
                mario_bsp_straddlers,
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
        publish_frame_sample();
    }
}
int main(void) { user_init(); return 0; }
