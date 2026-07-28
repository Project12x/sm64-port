#include "saturn_demo_render.h"

#include <string.h>

#include "game/camera.h"
#include "saturn_gouraud.h"
#include "saturn_ir_texture.h"
#include "saturn_ir_transform.h"
#include "saturn_matrix_kernels.h"
#include "saturn_transform.h"
#include "bob_scene.h"
#include "saturn_mario_actor_mesh.h"
#include "../gpl/slavedriver_dma_queue.h"
#include "../gpl/slavedriver_dual_worker.h"
#include "../gpl/ztreme_hot_promotion.h"

#define DEMO_NEAR_DEPTH 128
#define DEMO_FAR_DEPTH 8192
#define DEMO_FOCAL_LENGTH 256
#define DEMO_CENTER_X 160
#define DEMO_CENTER_Y 112
#define DEMO_COORD_MIN (-1024)
#define DEMO_COORD_MAX 1023
#define DEMO_BUCKETS 16U
#define DEMO_CANCEL_POLL_INTERVAL 16U
#ifndef SATURN_DEMO_VIEW_RADIUS
#define SATURN_DEMO_VIEW_RADIUS 6000
#endif
#ifndef SATURN_SLAVE_RENDER
#define SATURN_SLAVE_RENDER 1
#endif
#ifndef SATURN_DEMO_POLY_TIER
#define SATURN_DEMO_POLY_TIER 0
#endif
#ifndef SATURN_DEMO_HOT_PROMOTION
#define SATURN_DEMO_HOT_PROMOTION 0
#endif

static sm64_saturn_vec3i_t s_view[SM64_SATURN_BOB_POSITION_COUNT];
static sm64_saturn_projected_vertex_t s_projected[
    SM64_SATURN_BOB_POSITION_COUNT];
static uint8_t s_position_valid[SM64_SATURN_BOB_POSITION_COUNT];
static uint16_t s_bucket_counts[DEMO_BUCKETS];
static uint16_t s_bucket_indices[DEMO_BUCKETS][
    SM64_SATURN_BOB_PRIMITIVE_COUNT];
static sm64_saturn_projected_vertex_t s_actor_projected[SM64_MARIO_VERTEX_COUNT];
static uint8_t s_actor_valid[SM64_MARIO_VERTEX_COUNT];
static int32_t s_bob_positions_resident[SM64_SATURN_BOB_POSITION_COUNT][3]
    __attribute__((section(".lwram_bss")));
static sm64_saturn_bob_primitive_t s_bob_primitives_resident[
    SM64_SATURN_BOB_PRIMITIVE_COUNT]
    __attribute__((section(".lwram_bss")));
#if SATURN_DEMO_HOT_PROMOTION
/* Optional Z-Treme-style hot arena. The source bank remains the LWRAM
 * authority; these HWRAM arrays are populated once before the frame loop and
 * then become the renderer's active read-only bank. */
static int32_t s_bob_positions_hot[SM64_SATURN_BOB_POSITION_COUNT][3]
    __attribute__((aligned(16)));
static sm64_saturn_bob_primitive_t s_bob_primitives_hot[
    SM64_SATURN_BOB_PRIMITIVE_COUNT] __attribute__((aligned(16)));
static saturn_hot_promotion_t s_bob_hot_promotion;
#endif
static const int32_t (*s_bob_positions_active)[3];
static const sm64_saturn_bob_primitive_t *s_bob_primitives_active;
static uint8_t s_bob_resident_ready;
static uint16_t s_slave_begin = SM64_SATURN_BOB_POSITION_COUNT / 2U;

typedef struct demo_transform_context {
    const sm64_saturn_ir_transform_job_t *job;
    const int32_t (*positions)[3];
    sm64_saturn_vec3i_t *view;
    sm64_saturn_projected_vertex_t *projected;
    uint8_t *valid;
    uint32_t transformed[2];
} demo_transform_context_t;

/* The generated BOB bank currently has one source-faithful primitive tier.
 * Keep the selection boundary explicit so near/mid/far baked variants can be
 * added without changing traversal or command ownership. Tiers 1 and 2 are
 * intentionally aliases of the full bank until those assets exist. */
static bool demo_poly_tier_accepts(const sm64_saturn_bob_primitive_t *primitive)
{
    (void)primitive;
    return SATURN_DEMO_POLY_TIER <= 2;
}

static void demo_transform_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_transform_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t i = begin; i < end; i++) {
        /* The cancellation latch lives in uncached shared control memory.
         * Poll at a bounded interval rather than once per vertex: the
         * worker's callback is finite and the outer wait is already bounded,
         * while per-vertex reads turn the shared bus into the hot path. */
        if (((uint16_t)(i - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled()) break;
        const int64_t dx = (int64_t)context->positions[i][0] -
                           context->job->camera.position.x;
        const int64_t dy = (int64_t)context->positions[i][1] -
                           context->job->camera.position.y;
        const int64_t dz = (int64_t)context->positions[i][2] -
                           context->job->camera.position.z;
        if (dx * dx + dy * dy + dz * dz >
            (int64_t)SATURN_DEMO_VIEW_RADIUS * SATURN_DEMO_VIEW_RADIUS) {
            context->valid[i] = 0U;
            continue;
        }
        if (sm64_saturn_ir_transform_one(
                context->job,
                (sm64_saturn_vec3i_t){context->positions[i][0],
                                      context->positions[i][1],
                                      context->positions[i][2]},
                &context->view[i], &context->projected[i])) {
            context->valid[i] = 1U;
            context->transformed[lane]++;
        } else {
            context->valid[i] = 0U;
            context->view[i] = (sm64_saturn_vec3i_t){0, 0, DEMO_NEAR_DEPTH};
            context->projected[i] = (sm64_saturn_projected_vertex_t){
                DEMO_CENTER_X, DEMO_CENTER_Y, DEMO_NEAR_DEPTH};
        }
    }
}

void sm64_saturn_demo_render_init(void)
{
    memcpy(s_bob_positions_resident, sm64_saturn_bob_positions,
           sizeof(s_bob_positions_resident));
    memcpy(s_bob_primitives_resident, sm64_saturn_bob_primitives,
           sizeof(s_bob_primitives_resident));
    s_bob_positions_active = s_bob_positions_resident;
    s_bob_primitives_active = s_bob_primitives_resident;
#if SATURN_DEMO_HOT_PROMOTION
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_positions_hot,
        sizeof(s_bob_positions_hot));
    const int32_t (*hot_positions)[3] = saturn_hot_promote(
        &s_bob_hot_promotion, s_bob_positions_resident,
        sizeof(s_bob_positions_resident), 16U);
    /* The second bank is a separate bounded arena entry. Resetting to its
     * own HWRAM destination keeps both source ranges independently checked. */
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_primitives_hot,
        sizeof(s_bob_primitives_hot));
    const sm64_saturn_bob_primitive_t *hot_primitives = saturn_hot_promote(
        &s_bob_hot_promotion, s_bob_primitives_resident,
        sizeof(s_bob_primitives_resident), 16U);
    if (hot_positions != NULL && hot_primitives != NULL) {
        s_bob_positions_active = hot_positions;
        s_bob_primitives_active = hot_primitives;
    }
#endif
    s_bob_resident_ready = 1U;
}

static int32_t demo_world_unit(float value)
{
    return (int32_t)value;
}

static sm64_saturn_camera_transform_t demo_camera(
    const sm64_saturn_mario_actor_snapshot_t *snapshot)
{
    const sm64_saturn_vec3i_t position = {
        demo_world_unit(snapshot->camera_position[0]),
        demo_world_unit(snapshot->camera_position[1]),
        demo_world_unit(snapshot->camera_position[2])
    };
    const sm64_saturn_vec3i_t focus = {
        demo_world_unit(snapshot->camera_focus[0]),
        demo_world_unit(snapshot->camera_focus[1]),
        demo_world_unit(snapshot->camera_focus[2])
    };
    const sm64_saturn_vec3i_t forward = sm64_saturn_vec3_normalize_q16(
        (sm64_saturn_vec3i_t){focus.x - position.x, focus.y - position.y,
                              focus.z - position.z});
    const sm64_saturn_vec3i_t right = sm64_saturn_vec3_normalize_q16(
        (sm64_saturn_vec3i_t){-forward.z, 0, forward.x});
    const sm64_saturn_vec3i_t up = {
        (int32_t)(-(int64_t)right.z * forward.y >> 16),
        (int32_t)(((int64_t)right.z * forward.x -
                   (int64_t)right.x * forward.z) >> 16),
        (int32_t)((int64_t)right.x * forward.y >> 16)
    };
    return (sm64_saturn_camera_transform_t){position, right, up, forward};
}

static uint16_t demo_bucket(int32_t z)
{
    if (z <= DEMO_NEAR_DEPTH) return 0;
    if (z >= DEMO_FAR_DEPTH) return DEMO_BUCKETS - 1U;
    return (uint16_t)(((z - DEMO_NEAR_DEPTH) *
                       (DEMO_BUCKETS - 1U)) /
                      (DEMO_FAR_DEPTH - DEMO_NEAR_DEPTH));
}

static void demo_emit_primitive(
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const vdp1_vram_partitions_t *partitions)
{
    const uint16_t *indices = primitive->indices;
    const int16_vec2_t vertices[4] = {
        INT16_VEC2_INITIALIZER(s_projected[indices[0]].x,
                               s_projected[indices[0]].y),
        INT16_VEC2_INITIALIZER(s_projected[indices[1]].x,
                               s_projected[indices[1]].y),
        INT16_VEC2_INITIALIZER(s_projected[indices[2]].x,
                               s_projected[indices[2]].y),
        INT16_VEC2_INITIALIZER(s_projected[indices[3]].x,
                               s_projected[indices[3]].y)
    };
    const int32_t cross = (int32_t)(vertices[1].x - vertices[0].x) *
                              (vertices[2].y - vertices[0].y) -
                          (int32_t)(vertices[1].y - vertices[0].y) *
                              (vertices[2].x - vertices[0].x);
    if (cross == 0) {
        profile->reject_degenerate++;
        return;
    }

    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
    if (cmdt == NULL) {
        profile->reject_vdp1_arena_capacity++;
        return;
    }
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, vertices);
    if (primitive->textured != 0U) {
        const bool bound = sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, vertices);
        if (bound) {
            profile->texture_commands++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
    }
    sm64_saturn_gouraud_table_t *table = NULL;
    uintptr_t gouraud_address = 0;
    if (primitive->textured == 0U) {
        table = sm64_saturn_gouraud_bank_alloc(gouraud_bank,
                                                &gouraud_address);
    }
    if (table != NULL) {
        const rgb1555_t color = RGB1555(1, primitive->rgb[0],
                                        primitive->rgb[1], primitive->rgb[2]);
        table->colors[0] = color.raw;
        table->colors[1] = color.raw;
        table->colors[2] = color.raw;
        table->colors[3] = color.raw;
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_GOURAUD});
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = sm64_saturn_gouraud_neutral_color()});
        vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)gouraud_address);
    } else {
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                          primitive->rgb[1], primitive->rgb[2]));
        if (primitive->textured == 0U) profile->gouraud_bank_overflow++;
    }
    profile->triangles_vdp1_emitted++;
    profile->triangles_emitted++;
}

static void demo_emit_mario(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid ||
        pose->vertices == NULL || pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return;
    const int32_t sine = sm64_saturn_sins_q16(snapshot->yaw);
    const int32_t cosine = sm64_saturn_coss_q16(snapshot->yaw);
    const sm64_saturn_ir_transform_job_t job = {
        .camera = demo_camera(snapshot), .focal_length = DEMO_FOCAL_LENGTH,
        .near_depth = DEMO_NEAR_DEPTH, .center_x = DEMO_CENTER_X,
        .center_y = DEMO_CENTER_Y, .coord_min = DEMO_COORD_MIN,
        .coord_max = DEMO_COORD_MAX
    };
    for (uint16_t i = 0; i < SM64_MARIO_VERTEX_COUNT; i++) {
        const int16_t *source = pose->vertices[i];
        const int32_t sx = (int32_t)source[0] << 16;
        const int32_t sz = (int32_t)source[2] << 16;
        const sm64_saturn_vec3i_t world = {
            (int32_t)snapshot->position[0] +
                ((sm64_saturn_q16_mul(sx, cosine) +
                  sm64_saturn_q16_mul(sz, sine)) >> 16),
            (int32_t)snapshot->position[1] + source[1],
            (int32_t)snapshot->position[2] +
                ((-sm64_saturn_q16_mul(sx, sine) +
                  sm64_saturn_q16_mul(sz, cosine)) >> 16)};
        sm64_saturn_vec3i_t view;
        s_actor_valid[i] = sm64_saturn_ir_transform_one(
            &job, world, &view, &s_actor_projected[i]) ? 1U : 0U;
        if (s_actor_valid[i]) profile->demo_actor_vertices_valid++;
    }
    for (uint16_t i = 0; i < SM64_MARIO_PRIMITIVE_COUNT; i++) {
        const uint16_t *primitive = sm64_mario_primitives[i];
        if (!s_actor_valid[primitive[1]] || !s_actor_valid[primitive[2]] ||
            !s_actor_valid[primitive[3]] || !s_actor_valid[primitive[4]])
            continue;
        const int16_vec2_t vertices[4] = {
            INT16_VEC2_INITIALIZER(s_actor_projected[primitive[1]].x,
                                   s_actor_projected[primitive[1]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[primitive[2]].x,
                                   s_actor_projected[primitive[2]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[primitive[3]].x,
                                   s_actor_projected[primitive[3]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[primitive[4]].x,
                                   s_actor_projected[primitive[4]].y)};
        const int32_t cross = (int32_t)(vertices[1].x - vertices[0].x) *
                                  (vertices[2].y - vertices[0].y) -
                              (int32_t)(vertices[1].y - vertices[0].y) *
                                  (vertices[2].x - vertices[0].x);
        if (cross == 0) continue;
        vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
        if (cmdt == NULL) {
            profile->reject_vdp1_arena_capacity++;
            continue;
        }
        const uint8_t *rgb = sm64_mario_material_rgb[primitive[0]];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
        vdp1_cmdt_vtx_set(cmdt, vertices);
        profile->triangles_vdp1_emitted++;
        profile->triangles_emitted++;
        profile->demo_actor_primitives_emitted++;
    }
}

void sm64_saturn_demo_render_frame(
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose)
{
    if (!s_bob_resident_ready) return;
    const sm64_saturn_ir_transform_job_t job = {
        .camera = demo_camera(snapshot),
        .focal_length = DEMO_FOCAL_LENGTH,
        .near_depth = DEMO_NEAR_DEPTH,
        .center_x = DEMO_CENTER_X,
        .center_y = DEMO_CENTER_Y,
        .coord_min = DEMO_COORD_MIN,
        .coord_max = DEMO_COORD_MAX
    };
    demo_transform_context_t transform = {
        .job = &job,
        .positions = s_bob_positions_active,
        .view = s_view,
        .projected = s_projected,
        .valid = s_position_valid,
        .transformed = {0U, 0U}
    };
    sm64_saturn_dual_worker_stats_t worker_stats;
    bool worker_ok = true;
#if SATURN_SLAVE_RENDER
    worker_ok = sm64_saturn_dual_worker_run(
        demo_transform_range, &transform, SM64_SATURN_BOB_POSITION_COUNT,
        s_slave_begin, &worker_stats);
#else
    worker_stats = (sm64_saturn_dual_worker_stats_t){0};
    demo_transform_range(&transform, 0U, SM64_SATURN_BOB_POSITION_COUNT);
#endif
    if (!worker_ok) {
        transform.transformed[0] = 0U;
        transform.transformed[1] = 0U;
        demo_transform_range(&transform, 0U,
                             SM64_SATURN_BOB_POSITION_COUNT);
    }
    profile->triangles_transformed += transform.transformed[0] +
                                     transform.transformed[1];
    profile->slave_jobs_completed += worker_stats.slave_jobs_completed;
    profile->slave_busy_ticks += worker_stats.slave_busy_ticks;
    profile->master_wait_ticks += worker_stats.master_wait_ticks;
    profile->slave_timeouts += worker_stats.slave_timeouts;
    if (worker_ok && worker_stats.slave_busy_ticks != 0U) {
        /* SlaveDriver's measured spin-count balancer: if the master waited
         * longer than its 100-tick threshold, give the slave fewer vertices;
         * otherwise let it take one more. The boundary is bounded so both
         * CPUs retain work, matching the upstream monotonic adjustment. */
        if (worker_stats.master_wait_ticks > 100U && s_slave_begin + 8U <
            SM64_SATURN_BOB_POSITION_COUNT) {
            s_slave_begin++;
        } else if (worker_stats.master_wait_ticks < 100U &&
                   s_slave_begin > 8U) {
            s_slave_begin--;
        }
    }
    memset(s_bucket_counts, 0, sizeof(s_bucket_counts));
    for (uint16_t i = 0; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++) {
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[i];
        if (!demo_poly_tier_accepts(primitive)) continue;
        if (!s_position_valid[primitive->indices[0]] ||
            !s_position_valid[primitive->indices[1]] ||
            !s_position_valid[primitive->indices[2]] ||
            !s_position_valid[primitive->indices[3]]) {
            continue;
        }
        const int32_t z = (s_projected[primitive->indices[0]].z +
                           s_projected[primitive->indices[1]].z +
                           s_projected[primitive->indices[2]].z) / 3;
        const uint16_t bucket = demo_bucket(z);
        s_bucket_indices[bucket][s_bucket_counts[bucket]++] = i;
    }
    sm64_saturn_gouraud_bank_begin(gouraud_bank);
    sm64_saturn_vdp1_backend_begin(backend);
    /* This layout is immutable after sourceboot initialization. Hoist the
     * shadow-register query out of the per-primitive master-side loop; the
     * slave only transforms disjoint arrays and never touches submission
     * state. */
    vdp1_vram_partitions_t partitions;
    vdp1_vram_partitions_get(&partitions);
    for (int bucket = (int)DEMO_BUCKETS - 1; bucket >= 0; bucket--) {
        for (uint16_t ordinal = 0; ordinal < s_bucket_counts[bucket]; ordinal++) {
            demo_emit_primitive(
                &s_bob_primitives_active[
                    s_bucket_indices[bucket][ordinal]], backend, gouraud_bank,
                profile, &partitions);
        }
    }
    /* Mario is currently a flat-material actor pass. It deliberately consumes
     * the live bridge pose now; textured actor tiles and painter interleave
     * remain separate fidelity work, rather than hiding the actor seam. */
    demo_emit_mario(snapshot, pose, backend, profile);
    if (sm64_saturn_gouraud_bank_used_bytes(gouraud_bank) > 0U) {
        saturn_dma_queue_transfer_wait(
            (void *)gouraud_bank->vram_base, gouraud_bank->staging,
            sm64_saturn_gouraud_bank_used_bytes(gouraud_bank),
            SATURN_DMA_QUEUE_SCU);
    }
    sm64_saturn_vdp1_backend_finish(backend);
    sm64_saturn_vdp1_backend_upload(backend);
    profile->frame_serial++;
}
