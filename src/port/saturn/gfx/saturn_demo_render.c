#include "saturn_demo_render.h"

#include <string.h>
#include <stdlib.h>

#include "game/camera.h"
#include "saturn_gouraud.h"
#include "saturn_ir_texture.h"
#include "saturn_ir_transform.h"
#include "saturn_matrix_kernels.h"
#include "saturn_transform.h"
#include "bob_scene.h"
#include "bob_bsp.h"
#if defined(SATURN_DEMO_BSP_FRAGMENTS) && SATURN_DEMO_BSP_FRAGMENTS
#include "bob_bsp_fragments.h"
#undef SM64_SATURN_BOB_POSITION_COUNT
#undef SM64_SATURN_BOB_PRIMITIVE_COUNT
#undef sm64_saturn_bob_positions
#undef sm64_saturn_bob_primitives
#define SM64_SATURN_BOB_POSITION_COUNT SM64_SATURN_BOB_FRAGMENT_POSITION_COUNT
#define SM64_SATURN_BOB_PRIMITIVE_COUNT SM64_SATURN_BOB_FRAGMENT_PRIMITIVE_COUNT
#define sm64_saturn_bob_positions sm64_saturn_bob_fragment_positions
#define sm64_saturn_bob_primitives sm64_saturn_bob_fragment_primitives
#define sm64_saturn_bob_primitive_t sm64_saturn_bob_fragment_primitive_t
#endif
#include "saturn_mario_actor_mesh.h"
#if defined(SATURN_DEMO_MARIO_TEXTURES)
#include "mario_eye_uv_tiles.h"
#endif
#include "../gpl/slavedriver_dma_queue.h"
#include "../gpl/slavedriver_dual_worker.h"
#include "../gpl/ztreme_hot_promotion.h"

#ifndef SATURN_DEMO_NEAR_DEPTH
#define SATURN_DEMO_NEAR_DEPTH 128
#endif
#define DEMO_FAR_DEPTH 8192
#define DEMO_FOCAL_LENGTH 256
#define DEMO_CENTER_X 160
#define DEMO_CENTER_Y 112
#define DEMO_COORD_MIN (-1024)
#define DEMO_COORD_MAX 1023
#ifndef SATURN_DEMO_BUCKETS
/* SGL/Z-Treme use finer depth staging than the original 16-pass bring-up
 * sweep. Thirty-two keeps the bounded SRAM footprint while reducing
 * same-bucket painter ambiguity for overlapping BOB terrain. */
#define SATURN_DEMO_BUCKETS 32U
#endif
#define DEMO_BUCKETS SATURN_DEMO_BUCKETS
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
#ifndef SATURN_DEMO_NEAR_CLIP
#define SATURN_DEMO_NEAR_CLIP 0
#endif
#ifndef SATURN_DEMO_BSP_ORDER
#define SATURN_DEMO_BSP_ORDER 1
#endif
#ifndef SATURN_DEMO_BSP_FRAGMENTS
#define SATURN_DEMO_BSP_FRAGMENTS 0
#endif

#if SATURN_DEMO_BSP_FRAGMENTS
#define DEMO_FRAGMENT_CACHE __attribute__((section(".lwram_bss")))
#else
#define DEMO_FRAGMENT_CACHE
#endif

static sm64_saturn_vec3i_t s_view[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_FRAGMENT_CACHE;
static sm64_saturn_projected_vertex_t s_projected[
    SM64_SATURN_BOB_POSITION_COUNT] DEMO_FRAGMENT_CACHE;
static uint8_t s_position_valid[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_FRAGMENT_CACHE;
static uint16_t s_bucket_counts[DEMO_BUCKETS];
static uint16_t s_bucket_offsets[DEMO_BUCKETS + 1U];
static uint16_t s_emit_order[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_emit_reordered[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_emit_count;
static uint8_t s_bsp_seen[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_visible[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_clipped[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static sm64_saturn_projected_vertex_t s_clipped_projected[
    SM64_SATURN_BOB_PRIMITIVE_COUNT][4] __attribute__((section(".lwram_bss")));
static uint8_t s_primitive_buckets[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static int32_t s_primitive_depth[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_primitive_slots[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static sm64_saturn_projected_vertex_t s_actor_projected[SM64_MARIO_VERTEX_COUNT];
static uint8_t s_actor_valid[SM64_MARIO_VERTEX_COUNT];
static uint16_t s_actor_order[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t s_actor_slots[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t s_actor_texture_slots[SM64_MARIO_PRIMITIVE_COUNT];

static uint16_t s_actor_texture_count;
static sm64_saturn_gouraud_table_t *s_actor_gouraud[
    SM64_MARIO_PRIMITIVE_COUNT];
static uintptr_t s_actor_gouraud_addresses[SM64_MARIO_PRIMITIVE_COUNT];
static const uint8_t *s_actor_light_intensity;
static uint16_t s_actor_draw_count;
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

#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
static void demo_bsp_append(int16_t node,
                            const sm64_saturn_camera_transform_t *camera)
{
    if (node < 0) return;
    const int64_t side =
        (int64_t)sm64_saturn_bob_bsp_planes[node][0] * camera->position.x +
        (int64_t)sm64_saturn_bob_bsp_planes[node][1] * camera->position.y +
        (int64_t)sm64_saturn_bob_bsp_planes[node][2] * camera->position.z +
        sm64_saturn_bob_bsp_distances[node];
    const bool camera_front = side >= 0;
    const int16_t far = sm64_saturn_bob_bsp_children[node][camera_front ? 1 : 0];
    const int16_t near = sm64_saturn_bob_bsp_children[node][camera_front ? 0 : 1];
    demo_bsp_append(far, camera);
    const uint16_t start = sm64_saturn_bob_bsp_ref_ranges[node][0];
    const uint16_t count = sm64_saturn_bob_bsp_ref_ranges[node][1];
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t primitive = sm64_saturn_bob_bsp_refs[start + offset];
        if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT ||
            s_primitive_visible[primitive] == 0U ||
            s_bsp_seen[primitive] != 0U || s_emit_count >=
                SM64_SATURN_BOB_PRIMITIVE_COUNT)
            continue;
        s_bsp_seen[primitive] = 1U;
        s_emit_order[s_emit_count++] = primitive;
    }
    demo_bsp_append(near, camera);
}
#endif

#if SATURN_DEMO_BSP_ORDER && SATURN_DEMO_BSP_FRAGMENTS
/* Fragment commands retain their compiled Mesh IR primitive identity in
 * source0. Traverse the same camera-dependent BSP as the unsplit path, then
 * expand each source primitive into all of its baked split fragments. This
 * preserves the static-world dependency stream without pretending each
 * independently split polygon has its own plane. */
static uint16_t s_fragment_source_order[SM64_SATURN_BOB_BSP_REF_COUNT];
static uint8_t s_fragment_source_seen[SM64_SATURN_BOB_BSP_REF_COUNT];
static uint16_t s_fragment_source_count;

static void demo_fragment_bsp_append_sources(
    int16_t node, const sm64_saturn_camera_transform_t *camera)
{
    if (node < 0) return;
    const int64_t side =
        (int64_t)sm64_saturn_bob_bsp_planes[node][0] * camera->position.x +
        (int64_t)sm64_saturn_bob_bsp_planes[node][1] * camera->position.y +
        (int64_t)sm64_saturn_bob_bsp_planes[node][2] * camera->position.z +
        sm64_saturn_bob_bsp_distances[node];
    const bool camera_front = side >= 0;
    const int16_t far = sm64_saturn_bob_bsp_children[node][camera_front ? 1 : 0];
    const int16_t near = sm64_saturn_bob_bsp_children[node][camera_front ? 0 : 1];
    demo_fragment_bsp_append_sources(far, camera);
    const uint16_t start = sm64_saturn_bob_bsp_ref_ranges[node][0];
    const uint16_t count = sm64_saturn_bob_bsp_ref_ranges[node][1];
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t source = sm64_saturn_bob_bsp_refs[start + offset];
        if (source >= SM64_SATURN_BOB_BSP_REF_COUNT ||
            s_fragment_source_seen[source] != 0U ||
            s_fragment_source_count >= SM64_SATURN_BOB_BSP_REF_COUNT)
            continue;
        s_fragment_source_seen[source] = 1U;
        s_fragment_source_order[s_fragment_source_count++] = source;
    }
    demo_fragment_bsp_append_sources(near, camera);
}
#endif

static void demo_build_clipped_quad(
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_projected_vertex_t output[4])
{
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        const uint16_t index = primitive->indices[corner];
        output[corner] = s_projected[index];
        if (s_view[index].z > SATURN_DEMO_NEAR_DEPTH) continue;

        const uint8_t next = (uint8_t)((corner + 1U) & 3U);
        const uint8_t previous = (uint8_t)((corner + 3U) & 3U);
        uint8_t front = UINT8_MAX;
        if (s_view[primitive->indices[next]].z > SATURN_DEMO_NEAR_DEPTH &&
            s_view[primitive->indices[previous]].z <=
                SATURN_DEMO_NEAR_DEPTH) {
            front = next;
        } else if (
            s_view[primitive->indices[previous]].z > SATURN_DEMO_NEAR_DEPTH &&
            s_view[primitive->indices[next]].z <= SATURN_DEMO_NEAR_DEPTH) {
            front = previous;
        }
        if (front == UINT8_MAX &&
            s_view[primitive->indices[next]].z > SATURN_DEMO_NEAR_DEPTH &&
            s_view[primitive->indices[previous]].z >
                SATURN_DEMO_NEAR_DEPTH) {
            /* An isolated back corner has two valid edge intersections;
             * choose the next edge deterministically. */
            front = next;
        }
        if (front == UINT8_MAX) continue;

        const sm64_saturn_projected_vertex_t edge =
            s_projected[primitive->indices[front]];
        const int32_t back_z = s_view[index].z;
        const int32_t front_z = s_view[primitive->indices[front]].z;
        const int32_t denominator = front_z - back_z;
        if (denominator <= 0) continue;
        const int32_t ratio = (int32_t)(((int64_t)(front_z -
            SATURN_DEMO_NEAR_DEPTH) << 16) / denominator);
        output[corner].x = (int16_t)(edge.x -
            (int32_t)(((int64_t)(edge.x - output[corner].x) * ratio) >> 16));
        output[corner].y = (int16_t)(edge.y -
            (int32_t)(((int64_t)(edge.y - output[corner].y) * ratio) >> 16));
        output[corner].z = SATURN_DEMO_NEAR_DEPTH;
    }
}

static void demo_primitive_screen_vertices(
    const sm64_saturn_bob_primitive_t *primitive,
    int16_vec2_t vertices[4])
{
    const sm64_saturn_projected_vertex_t *projected =
        s_primitive_clipped[primitive - s_bob_primitives_active] != 0U
            ? s_clipped_projected[primitive - s_bob_primitives_active]
            : NULL;
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        const sm64_saturn_projected_vertex_t point = projected != NULL
            ? projected[corner] : s_projected[primitive->indices[corner]];
        vertices[corner].x = point.x;
        vertices[corner].y = point.y;
    }
}

typedef struct demo_transform_context {
    const sm64_saturn_ir_transform_job_t *job;
    const int32_t (*positions)[3];
    sm64_saturn_vec3i_t *view;
    sm64_saturn_projected_vertex_t *projected;
    uint8_t *valid;
    uint32_t transformed[2];
} demo_transform_context_t;

typedef struct demo_mario_transform_context {
    const sm64_saturn_ir_transform_job_t *job;
    const sm64_saturn_mario_actor_snapshot_t *snapshot;
    const sm64_saturn_mario_actor_pose_t *pose;
} demo_mario_transform_context_t;

typedef struct demo_scene_transform_context {
    demo_transform_context_t *terrain;
    demo_mario_transform_context_t *mario;
    uint16_t terrain_count;
    uint16_t mario_count;
} demo_scene_transform_context_t;

typedef struct demo_classify_context {
    const sm64_saturn_bob_primitive_t *primitives;
    const sm64_saturn_camera_transform_t *camera;
    uint32_t visible[2];
    uint32_t radius_rejected[2];
    uint32_t near_rejected[2];
    uint32_t degenerate[2];
} demo_classify_context_t;

typedef struct demo_emit_stats {
    uint32_t triangles_emitted;
    uint32_t texture_commands;
    uint32_t gouraud_bank_overflow;
} demo_emit_stats_t;

typedef struct demo_emit_context {
    const sm64_saturn_bob_primitive_t *primitives;
    vdp1_cmdt_t *cmdts;
    const vdp1_vram_partitions_t *partitions;
    demo_emit_stats_t stats[2];
} demo_emit_context_t;

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
        /* Do not radius-cull individual vertices here.  Z-Treme's tri-state
         * bounds and SlaveDriver's sector AABBs both keep a spatial unit
         * alive when only part of it intersects the view.  Primitive-level
         * bounds below decide visibility after all corners have a stable
         * projection; this prevents a large terrain face from popping merely
         * because one corner crossed the distance sphere. */
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
            context->view[i] = (sm64_saturn_vec3i_t){0, 0,
                                                     SATURN_DEMO_NEAR_DEPTH};
            context->projected[i] = (sm64_saturn_projected_vertex_t){
                DEMO_CENTER_X, DEMO_CENTER_Y, SATURN_DEMO_NEAR_DEPTH};
        }
    }
}

static void demo_transform_mario_range(void *opaque, uint16_t begin,
                                       uint16_t end)
{
    demo_mario_transform_context_t *context = opaque;
    if (context->snapshot == NULL || context->pose == NULL ||
        !context->snapshot->valid || context->pose->vertices == NULL ||
        context->pose->vertex_count != SM64_MARIO_VERTEX_COUNT) {
        return;
    }
    const int32_t sine = sm64_saturn_sins_q16(context->snapshot->yaw);
    const int32_t cosine = sm64_saturn_coss_q16(context->snapshot->yaw);
    for (uint16_t i = begin; i < end; i++) {
        if (((uint16_t)(i - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled()) break;
        const int16_t *source = context->pose->vertices[i];
        const int32_t sx = (int32_t)source[0] << 16;
        const int32_t sz = (int32_t)source[2] << 16;
        const sm64_saturn_vec3i_t world = {
            (int32_t)context->snapshot->position[0] +
                ((sm64_saturn_q16_mul(sx, cosine) +
                  sm64_saturn_q16_mul(sz, sine)) >> 16),
            (int32_t)context->snapshot->position[1] + source[1],
            (int32_t)context->snapshot->position[2] +
                ((-sm64_saturn_q16_mul(sx, sine) +
                  sm64_saturn_q16_mul(sz, cosine)) >> 16)};
        sm64_saturn_vec3i_t view;
        s_actor_valid[i] = sm64_saturn_ir_transform_one(
            context->job, world, &view, &s_actor_projected[i]) ? 1U : 0U;
    }
}

static void demo_transform_scene_range(void *opaque, uint16_t begin,
                                       uint16_t end)
{
    demo_scene_transform_context_t *context = opaque;
    demo_transform_range(context->terrain, begin, end);
    if (context->mario_count == 0U || context->terrain_count == 0U) return;
    const uint32_t mario_begin = (uint32_t)begin * context->mario_count /
                                 context->terrain_count;
    const uint32_t mario_end = (uint32_t)end * context->mario_count /
                               context->terrain_count;
    demo_transform_mario_range(context->mario, (uint16_t)mario_begin,
                               (uint16_t)mario_end);
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
    if (z <= SATURN_DEMO_NEAR_DEPTH) return 0;
    if (z >= DEMO_FAR_DEPTH) return DEMO_BUCKETS - 1U;
    return (uint16_t)(((z - SATURN_DEMO_NEAR_DEPTH) *
                       (DEMO_BUCKETS - 1U)) /
                      (DEMO_FAR_DEPTH - SATURN_DEMO_NEAR_DEPTH));
}

static bool demo_primitive_in_radius(
    const sm64_saturn_bob_primitive_t *primitive,
    const sm64_saturn_camera_transform_t *camera)
{
    const uint16_t count = primitive->source1 == 0xFFFFU ? 3U : 4U;
    int32_t minimum[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t maximum[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint16_t corner = 0U; corner < count; corner++) {
        const int32_t *point = s_bob_positions_active[
            primitive->indices[corner]];
        for (uint8_t axis = 0U; axis < 3U; axis++) {
            if (point[axis] < minimum[axis]) minimum[axis] = point[axis];
            if (point[axis] > maximum[axis]) maximum[axis] = point[axis];
        }
    }
    const int32_t center[3] = {
        (minimum[0] + maximum[0]) / 2,
        (minimum[1] + maximum[1]) / 2,
        (minimum[2] + maximum[2]) / 2
    };
    int64_t bound = 0;
    for (uint16_t corner = 0U; corner < count; corner++) {
        const int32_t *point = s_bob_positions_active[
            primitive->indices[corner]];
        const int64_t dx = (int64_t)point[0] - center[0];
        const int64_t dy = (int64_t)point[1] - center[1];
        const int64_t dz = (int64_t)point[2] - center[2];
        const int64_t extent = (dx < 0 ? -dx : dx) +
                               (dy < 0 ? -dy : dy) +
                               (dz < 0 ? -dz : dz);
        if (extent > bound) bound = extent;
    }
    const int64_t dx = (int64_t)center[0] - camera->position.x;
    const int64_t dy = (int64_t)center[1] - camera->position.y;
    const int64_t dz = (int64_t)center[2] - camera->position.z;
    const int64_t distance = dx * dx + dy * dy + dz * dz;
    const int64_t view = (int64_t)SATURN_DEMO_VIEW_RADIUS;
    const int64_t limit = view + bound;
    return distance <= limit * limit;
}

static void demo_classify_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_classify_context_t *context = opaque;
    const sm64_saturn_camera_transform_t *camera = context->camera;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t i = begin; i < end; i++) {
        if (((uint16_t)(i - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const sm64_saturn_bob_primitive_t *primitive =
            &context->primitives[i];
        if (!demo_poly_tier_accepts(primitive)) {
            s_primitive_visible[i] = 0U;
            continue;
        }
        if (!demo_primitive_in_radius(primitive, camera)) {
            context->radius_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        bool any_front = false;
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            if (s_view[primitive->indices[corner]].z >
                SATURN_DEMO_NEAR_DEPTH) {
                any_front = true;
                break;
            }
        }
        if (!s_position_valid[primitive->indices[0]] ||
            !s_position_valid[primitive->indices[1]] ||
            !s_position_valid[primitive->indices[2]] ||
            !s_position_valid[primitive->indices[3]] || !any_front) {
            context->near_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        s_primitive_clipped[i] = 0U;
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            if (s_view[primitive->indices[corner]].z <=
                SATURN_DEMO_NEAR_DEPTH) {
                s_primitive_clipped[i] = 1U;
                break;
            }
        }
        if (s_primitive_clipped[i] != 0U) {
            demo_build_clipped_quad(primitive, s_clipped_projected[i]);
        }
        /* Match the castleviewer/SlaveDriver painter contract: a primitive's
         * farthest projected corner owns its painter key.  Using the nearest
         * corner made large BOB quads jump in front of neighboring surfaces as
         * the camera moved, because a quad crossing a depth boundary was
         * classified as near before its far half had been painted.  Proper
         * BSP splitting remains the long-term fix; max-z is the conservative
         * unsplit fallback used by the reference path. */
        int32_t z = s_projected[primitive->indices[0]].z;
        for (uint8_t corner = 1U; corner < 4U; corner++) {
            const int32_t corner_z =
                s_projected[primitive->indices[corner]].z;
            if (corner_z > z) z = corner_z;
        }
        const int32_t cross =
            (int32_t)(s_projected[primitive->indices[1]].x -
                      s_projected[primitive->indices[0]].x) *
                (s_projected[primitive->indices[2]].y -
                 s_projected[primitive->indices[0]].y) -
            (int32_t)(s_projected[primitive->indices[1]].y -
                      s_projected[primitive->indices[0]].y) *
                (s_projected[primitive->indices[2]].x -
                 s_projected[primitive->indices[0]].x);
        if (cross == 0) {
            context->degenerate[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        s_primitive_buckets[i] = (uint8_t)demo_bucket(z);
        s_primitive_depth[i] = z;
        s_primitive_visible[i] = 1U;
        context->visible[lane]++;
    }
}

static void demo_emit_primitive(
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const vdp1_vram_partitions_t *partitions)
{
    int16_vec2_t vertices[4];
    demo_primitive_screen_vertices(primitive, vertices);
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        /* Castleviewer draws source triangles with the same affine companion
         * corner used by the distorted-sprite texture mapping.  Collapsing
         * the shape to C while passing the companion to VDP1's texture path
         * makes the geometry and UV homography disagree, producing the large
         * warped sheets seen in BOB.  Flat triangles retain the repeated-C
         * polygon convention. */
        primitive->source1 == 0xFFFFU && primitive->textured == 0U
            ? vertices[2] : vertices[3]
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
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
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

#if SATURN_SLAVE_RENDER
/* Direct-slot variant used by the dual-worker path. The caller has already
 * reserved and assigned the command slot in source draw order, so this
 * routine performs no shared arena mutation. The slave deliberately uses
 * flat RGB1555 for untextured primitives: Gouraud-table allocation remains a
 * master-owned resource, while textured setup is entirely disjoint. */
static void demo_emit_primitive_at(
    const sm64_saturn_bob_primitive_t *primitive,
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    bool allow_gouraud,
    demo_emit_stats_t *stats)
{
    int16_vec2_t vertices[4];
    demo_primitive_screen_vertices(primitive, vertices);
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        primitive->source1 == 0xFFFFU && primitive->textured == 0U
            ? vertices[2] : vertices[3]
    };
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (primitive->textured != 0U &&
        sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, vertices)) {
        stats->texture_commands++;
        stats->triangles_emitted++;
        return;
    }
    if (allow_gouraud) {
        /* The master fallback path retains the existing Gouraud allocator;
         * direct worker slots use flat color to avoid a shared cursor. */
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
    } else {
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
    }
    vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                      primitive->rgb[1], primitive->rgb[2]));
    if (primitive->textured == 0U) stats->gouraud_bank_overflow++;
    stats->triangles_emitted++;
}

static void demo_emit_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_emit_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t ordinal = begin; ordinal < end; ordinal++) {
        if (((uint16_t)(ordinal - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const uint16_t i = s_emit_order[ordinal];
        if (s_primitive_visible[i] == 0U) continue;
        demo_emit_primitive_at(&context->primitives[i],
                               &context->cmdts[s_primitive_slots[i]],
                               context->partitions, false,
                               &context->stats[lane]);
    }
}

static void demo_emit_mario_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_emit_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t ordinal = begin; ordinal < end; ordinal++) {
        if (((uint16_t)(ordinal - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const uint16_t primitive = s_actor_order[ordinal];
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const int16_vec2_t vertices[4] = {
            INT16_VEC2_INITIALIZER(s_actor_projected[indices[1]].x,
                                   s_actor_projected[indices[1]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[indices[2]].x,
                                   s_actor_projected[indices[2]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[indices[3]].x,
                                   s_actor_projected[indices[3]].y),
            INT16_VEC2_INITIALIZER(s_actor_projected[indices[4]].x,
                                   s_actor_projected[indices[4]].y)};
        vdp1_cmdt_t *cmdt = &context->cmdts[s_actor_slots[ordinal]];
        vdp1_cmdt_polygon_set(cmdt);
        const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
        sm64_saturn_gouraud_table_t *table = s_actor_gouraud[ordinal];
        if (table != NULL) {
            const uint16_t corners[4] = {indices[1], indices[2], indices[3],
                                         indices[4]};
            for (uint8_t corner = 0; corner < 4U; corner++) {
                const uint8_t intensity = s_actor_light_intensity != NULL
                    ? s_actor_light_intensity[corners[corner]] : 31U;
                const uint8_t r = (uint8_t)((rgb[0] * intensity) / 31U);
                const uint8_t g = (uint8_t)((rgb[1] * intensity) / 31U);
                const uint8_t b = (uint8_t)((rgb[2] * intensity) / 31U);
                table->colors[corner] = RGB1555(
                    1, r > 31U ? 31U : r, g > 31U ? 31U : g,
                    b > 31U ? 31U : b).raw;
            }
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()});
            vdp1_cmdt_gouraud_base_set(
                cmdt, (vdp1_vram_t)s_actor_gouraud_addresses[ordinal]);
        } else {
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
        }
        vdp1_cmdt_vtx_set(cmdt, vertices);
        const uint16_t texture_start = sm64_mario_texture_tile_start[primitive];
        if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE &&
            context->partitions != NULL) {
            const uint16_t *texture_indices =
                sm64_mario_textured_source_vertices[texture_start / 4U];
            const int16_vec2_t texture_vertices[4] = {
                INT16_VEC2_INITIALIZER(s_actor_projected[texture_indices[0]].x,
                                       s_actor_projected[texture_indices[0]].y),
                INT16_VEC2_INITIALIZER(s_actor_projected[texture_indices[1]].x,
                                       s_actor_projected[texture_indices[1]].y),
                INT16_VEC2_INITIALIZER(s_actor_projected[texture_indices[2]].x,
                                       s_actor_projected[texture_indices[2]].y),
                INT16_VEC2_INITIALIZER(s_actor_projected[texture_indices[2]].x,
                                       s_actor_projected[texture_indices[2]].y)};
            vdp1_cmdt_t *detail = &context->cmdts[s_actor_texture_slots[ordinal]];
            (void)sm64_saturn_ir_texture_bind_rgb1555(
                detail, context->partitions,
                SATURN_MARIO_TEXTURE_BASE_OFFSET +
                    (texture_start / 4U) *
                    (SM64_MARIO_TEXTURE_UV_TILE_WIDTH *
                     SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t)),
                SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                VDP1_CMDT_CC_REPLACE, texture_vertices);
            context->stats[lane].texture_commands++;
        }
        context->stats[lane].triangles_emitted++;
    }
}

#if defined(SM64_SATURN_VDP1_LWRAM_STAGING)
typedef struct demo_vdp1_upload_context {
    volatile const uint32_t *source;
    volatile uint32_t *destination;
} demo_vdp1_upload_context_t;

static void demo_upload_vdp1_range(void *opaque, uint16_t begin,
                                   uint16_t end)
{
    demo_vdp1_upload_context_t *context = opaque;
    for (uint16_t i = begin; i < end; i++) {
        if (((uint16_t)(i - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        context->destination[i] = context->source[i];
    }
}

static void demo_upload_vdp1_serial(sm64_saturn_vdp1_backend_t *backend)
{
    volatile const uint32_t *source = (volatile const uint32_t *)
        ((uintptr_t)backend->list.cmdts & ~CPU_ADDRESS_PARTITION_MASK);
    volatile uint32_t *destination = (volatile uint32_t *)VDP1_VRAM(0);
    uint32_t words = (uint32_t)backend->list.count *
                     (sizeof(vdp1_cmdt_t) / sizeof(uint32_t));
    while (words-- > 0U) *destination++ = *source++;
}

static void demo_upload_vdp1_dual(sm64_saturn_vdp1_backend_t *backend,
                                  sm64_saturn_dual_worker_stats_t *stats)
{
    vdp1_sync_wait();
    assert(!vdp1_sync_busy());
    demo_vdp1_upload_context_t context = {
        .source = (volatile const uint32_t *)
            ((uintptr_t)backend->list.cmdts & ~CPU_ADDRESS_PARTITION_MASK),
        .destination = (volatile uint32_t *)VDP1_VRAM(0)};
    const uint32_t words = (uint32_t)backend->list.count *
                           (sizeof(vdp1_cmdt_t) / sizeof(uint32_t));
    const bool completed = sm64_saturn_dual_worker_run(
        demo_upload_vdp1_range, &context, (uint16_t)words,
        (uint16_t)(words / 2U), stats);
    if (!completed) demo_upload_vdp1_serial(backend);
    vdp1_sync_force_put();
}
#endif
#endif

static void demo_emit_mario(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    const vdp1_vram_partitions_t *partitions,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid ||
        pose->vertices == NULL || pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return;
    memset(s_actor_gouraud, 0, sizeof(s_actor_gouraud));
    memset(s_actor_gouraud_addresses, 0, sizeof(s_actor_gouraud_addresses));
    s_actor_light_intensity = pose->light_intensity;
    for (uint16_t i = 0; i < SM64_MARIO_VERTEX_COUNT; i++)
        if (s_actor_valid[i]) profile->demo_actor_vertices_valid++;
#if SATURN_SLAVE_RENDER
    s_actor_draw_count = 0U;
    s_actor_texture_count = 0U;
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
        s_actor_order[s_actor_draw_count++] = i;
        if (sm64_mario_texture_tile_start[i] != SM64_MARIO_TEXTURE_TILE_NONE)
            s_actor_texture_count++;
    }
    /* VDP1 has no depth buffer. Castleviewer therefore paints Mario leaves
     * from far to near; source primitive order is only topology order and can
     * put a front-facing texture over the back of the actor. Keep the sort
     * stable for equal depths so captures remain deterministic. */
    for (uint16_t i = 1U; i < s_actor_draw_count; i++) {
        const uint16_t value = s_actor_order[i];
        const uint16_t *value_indices = sm64_mario_primitives[value];
        const int32_t value_depth =
            (s_actor_projected[value_indices[1]].z +
             s_actor_projected[value_indices[2]].z +
             s_actor_projected[value_indices[3]].z +
             s_actor_projected[value_indices[4]].z) / 4;
        uint16_t j = i;
        while (j > 0U) {
            const uint16_t previous = s_actor_order[j - 1U];
            const uint16_t *previous_indices = sm64_mario_primitives[previous];
            const int32_t previous_depth =
                (s_actor_projected[previous_indices[1]].z +
                 s_actor_projected[previous_indices[2]].z +
                 s_actor_projected[previous_indices[3]].z +
                 s_actor_projected[previous_indices[4]].z) / 4;
            if (previous_depth >= value_depth) break;
            s_actor_order[j] = previous;
            j--;
        }
        s_actor_order[j] = value;
    }
    const uint16_t actor_command_count =
        (uint16_t)(s_actor_draw_count + s_actor_texture_count);
    if (s_actor_draw_count != 0U &&
        sm64_saturn_vdp1_backend_reserve(backend, actor_command_count) != NULL) {
        uint16_t command_slot = (uint16_t)(backend->commands.cursor -
                                           actor_command_count);
        for (uint16_t i = 0; i < s_actor_draw_count; i++) {
            s_actor_slots[i] = command_slot++;
            if (sm64_mario_texture_tile_start[s_actor_order[i]] !=
                SM64_MARIO_TEXTURE_TILE_NONE)
                s_actor_texture_slots[i] = command_slot++;
        }
        for (uint16_t i = 0; i < s_actor_draw_count; i++) {
            s_actor_gouraud[i] = sm64_saturn_gouraud_bank_alloc(
                gouraud_bank, &s_actor_gouraud_addresses[i]);
            if (s_actor_gouraud[i] == NULL) profile->gouraud_bank_overflow++;
        }
        demo_emit_context_t actor_emit = {
            .partitions = partitions,
            .cmdts = backend->list.cmdts,
            .stats = {{0U, 0U, 0U}, {0U, 0U, 0U}}
        };
        sm64_saturn_dual_worker_stats_t actor_stats;
        const bool actor_ok = sm64_saturn_dual_worker_run(
            demo_emit_mario_range, &actor_emit, s_actor_draw_count,
            s_actor_draw_count / 2U, &actor_stats);
        profile->slave_jobs_completed += actor_stats.slave_jobs_completed;
        profile->slave_busy_ticks += actor_stats.slave_busy_ticks;
        profile->master_wait_ticks += actor_stats.master_wait_ticks;
        profile->slave_timeouts += actor_stats.slave_timeouts;
        if (actor_ok) {
            profile->triangles_vdp1_emitted +=
                actor_emit.stats[0].triangles_emitted +
                actor_emit.stats[1].triangles_emitted;
            profile->triangles_emitted +=
                actor_emit.stats[0].triangles_emitted +
                actor_emit.stats[1].triangles_emitted;
            profile->demo_actor_primitives_emitted += s_actor_draw_count;
            return;
        }
    }
#endif
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
        .near_depth = SATURN_DEMO_NEAR_DEPTH,
        .center_x = DEMO_CENTER_X,
        .center_y = DEMO_CENTER_Y,
        .coord_min = DEMO_COORD_MIN,
        .coord_max = DEMO_COORD_MAX,
        .clip_near = SATURN_DEMO_NEAR_CLIP != 0
    };
    demo_transform_context_t transform = {
        .job = &job,
        .positions = s_bob_positions_active,
        .view = s_view,
        .projected = s_projected,
        .valid = s_position_valid,
        .transformed = {0U, 0U}
    };
    demo_mario_transform_context_t mario_transform = {
        .job = &job, .snapshot = snapshot, .pose = pose
    };
    demo_scene_transform_context_t scene_transform = {
        .terrain = &transform,
        .mario = &mario_transform,
        .terrain_count = SM64_SATURN_BOB_POSITION_COUNT,
        .mario_count = SM64_MARIO_VERTEX_COUNT
    };
    sm64_saturn_dual_worker_stats_t worker_stats;
    bool worker_ok = true;
#if SATURN_SLAVE_RENDER
    worker_ok = sm64_saturn_dual_worker_run(
        demo_transform_scene_range, &scene_transform,
        SM64_SATURN_BOB_POSITION_COUNT,
        s_slave_begin, &worker_stats);
#else
    worker_stats = (sm64_saturn_dual_worker_stats_t){0};
    demo_transform_scene_range(&scene_transform, 0U,
                               SM64_SATURN_BOB_POSITION_COUNT);
#endif
    if (!worker_ok) {
        transform.transformed[0] = 0U;
        transform.transformed[1] = 0U;
        demo_transform_scene_range(&scene_transform, 0U,
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
    demo_classify_context_t classify = {
        .primitives = s_bob_primitives_active,
        .camera = &job.camera,
        .visible = {0U, 0U},
        .radius_rejected = {0U, 0U},
        .near_rejected = {0U, 0U},
        .degenerate = {0U, 0U}
    };
    sm64_saturn_dual_worker_stats_t classify_stats;
    bool classify_ok = true;
#if SATURN_SLAVE_RENDER
    classify_ok = sm64_saturn_dual_worker_run(
        demo_classify_range, &classify, SM64_SATURN_BOB_PRIMITIVE_COUNT,
        SM64_SATURN_BOB_PRIMITIVE_COUNT / 2U, &classify_stats);
#else
    classify_stats = (sm64_saturn_dual_worker_stats_t){0};
    demo_classify_range(&classify, 0U, SM64_SATURN_BOB_PRIMITIVE_COUNT);
#endif
    if (!classify_ok) {
        memset(s_primitive_visible, 0, sizeof(s_primitive_visible));
        demo_classify_range(&classify, 0U, SM64_SATURN_BOB_PRIMITIVE_COUNT);
    }
    profile->slave_jobs_completed += classify_stats.slave_jobs_completed;
    profile->slave_busy_ticks += classify_stats.slave_busy_ticks;
    profile->master_wait_ticks += classify_stats.master_wait_ticks;
    profile->slave_timeouts += classify_stats.slave_timeouts;
    profile->demo_bob_primitives_visible += classify.visible[0] +
                                            classify.visible[1];
    profile->demo_bob_primitives_radius_rejected +=
        classify.radius_rejected[0] + classify.radius_rejected[1];
    profile->demo_bob_primitives_near_rejected +=
        classify.near_rejected[0] + classify.near_rejected[1];
    profile->demo_bob_primitives_degenerate +=
        classify.degenerate[0] + classify.degenerate[1];

#if SATURN_DEMO_BSP_ORDER
#if SATURN_DEMO_BSP_FRAGMENTS
    memset(s_bsp_seen, 0, sizeof(s_bsp_seen));
    memset(s_fragment_source_seen, 0, sizeof(s_fragment_source_seen));
    s_fragment_source_count = 0U;
    s_emit_count = 0U;
    demo_fragment_bsp_append_sources(0, &job.camera);
    for (uint16_t source_ordinal = 0U;
         source_ordinal < s_fragment_source_count; source_ordinal++) {
        const uint16_t source = s_fragment_source_order[source_ordinal];
        for (uint16_t primitive = 0U;
             primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++) {
            if (s_primitive_visible[primitive] == 0U ||
                s_bsp_seen[primitive] != 0U ||
                s_bob_primitives_active[primitive].source0 != source ||
                s_emit_count >= SM64_SATURN_BOB_PRIMITIVE_COUNT)
                continue;
            s_bsp_seen[primitive] = 1U;
            s_emit_order[s_emit_count++] = primitive;
        }
    }
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++) {
        if (s_primitive_visible[primitive] != 0U &&
            s_bsp_seen[primitive] == 0U)
            s_emit_order[s_emit_count++] = primitive;
    }
#else
    /* Z-Treme/castleviewer-style camera traversal supplies a stable
     * far-to-near dependency stream for the static world. */
    memset(s_bsp_seen, 0, sizeof(s_bsp_seen));
    s_emit_count = 0U;
    demo_bsp_append(0, &job.camera);
    for (uint16_t i = 0U; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++) {
        if (s_primitive_visible[i] != 0U && s_bsp_seen[i] == 0U)
            s_emit_order[s_emit_count++] = i;
    }
#endif
#else
    memset(s_bucket_counts, 0, sizeof(s_bucket_counts));
    s_emit_count = 0U;
    for (uint16_t i = 0; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++) {
        if (s_primitive_visible[i] == 0U) continue;
        const uint16_t bucket = s_primitive_buckets[i];
        s_bucket_counts[bucket]++;
    }
    s_bucket_offsets[0] = 0U;
    for (uint16_t bucket = 0U; bucket < DEMO_BUCKETS; bucket++)
        s_bucket_offsets[bucket + 1U] =
            s_bucket_offsets[bucket] + s_bucket_counts[bucket];
    uint16_t bucket_write[DEMO_BUCKETS];
    memcpy(bucket_write, s_bucket_offsets,
           sizeof(uint16_t) * DEMO_BUCKETS);
    for (uint16_t i = 0; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++) {
        if (s_primitive_visible[i] == 0U) continue;
        s_emit_order[bucket_write[s_primitive_buckets[i]]++] = i;
    }
    /* VDP1 has no depth buffer. Within each coarse bucket, order by the
     * farthest-corner view depth far-to-near; equal-depth primitives retain
     * source order for deterministic coplanar decals and seams. This matches
     * castleviewer's proven render queue and is the conservative unsplit
     * fallback until the baked BSP stream is wired into sourceboot. */
    for (uint16_t bucket = 0U; bucket < DEMO_BUCKETS; bucket++) {
        const uint16_t start = s_bucket_offsets[bucket];
        const uint16_t end = s_bucket_offsets[bucket + 1U];
        for (uint16_t i = start + 1U; i < end; i++) {
            const uint16_t value = s_emit_order[i];
            uint16_t j = i;
            while (j > start &&
                   s_primitive_depth[s_emit_order[j - 1U]] <
                       s_primitive_depth[value]) {
                s_emit_order[j] = s_emit_order[j - 1U];
                j--;
            }
            s_emit_order[j] = value;
        }
    }
    /* The bucket segments were built in near-to-far bucket order. Repack once
     * into the actual VDP1 far-to-near stream, preserving each bucket's
     * depth/source stability without a second bucket-sized matrix. */
    uint16_t reordered_count = 0U;
    for (int bucket = (int)DEMO_BUCKETS - 1; bucket >= 0; bucket--) {
        for (uint16_t ordinal = s_bucket_offsets[bucket];
             ordinal < s_bucket_offsets[bucket + 1U]; ordinal++) {
            s_emit_reordered[reordered_count++] = s_emit_order[ordinal];
        }
    }
    memcpy(s_emit_order, s_emit_reordered,
           sizeof(uint16_t) * reordered_count);
    s_emit_count = reordered_count;
#endif
    sm64_saturn_gouraud_bank_begin(gouraud_bank);
    sm64_saturn_vdp1_backend_begin(backend);
    uint16_t bob_draw_count = 0U;
    for (uint16_t ordinal = 0U; ordinal < s_emit_count; ordinal++) {
        const uint16_t primitive = s_emit_order[ordinal];
        s_primitive_slots[primitive] =
            (uint16_t)(backend->commands.cursor + bob_draw_count++);
    }
    vdp1_vram_partitions_t partitions;
    vdp1_vram_partitions_get(&partitions);
    demo_emit_context_t emit = {
        .primitives = s_bob_primitives_active,
        .cmdts = backend->list.cmdts,
        .partitions = &partitions,
        .stats = {{0U, 0U, 0U}, {0U, 0U, 0U}}
    };
    bool emit_direct = false;
#if SATURN_SLAVE_RENDER
    if (bob_draw_count != 0U &&
        sm64_saturn_vdp1_backend_reserve(backend, bob_draw_count) != NULL) {
        emit_direct = true;
        sm64_saturn_dual_worker_stats_t emit_worker_stats;
        const bool emit_ok = sm64_saturn_dual_worker_run(
            demo_emit_range, &emit, s_emit_count, s_emit_count / 2U,
            &emit_worker_stats);
        profile->slave_jobs_completed += emit_worker_stats.slave_jobs_completed;
        profile->slave_busy_ticks += emit_worker_stats.slave_busy_ticks;
        profile->master_wait_ticks += emit_worker_stats.master_wait_ticks;
        profile->slave_timeouts += emit_worker_stats.slave_timeouts;
        if (!emit_ok) {
            /* A failed worker has already observed cancellation; rerun the
             * complete direct range serially so every reserved slot is valid. */
            emit.stats[0] = (demo_emit_stats_t){0U, 0U, 0U};
            emit.stats[1] = (demo_emit_stats_t){0U, 0U, 0U};
            demo_emit_range(&emit, 0U, s_emit_count);
        }
    }
#endif
    if (!emit_direct) {
        /* Capacity fallback retains the pre-split path. This should remain
         * unreachable for the generated 867-primitive BOB bank, but avoids
         * turning a future asset expansion into a blank frame. */
        for (uint16_t ordinal = 0U; ordinal < s_emit_count; ordinal++) {
            demo_emit_primitive(
                &s_bob_primitives_active[s_emit_order[ordinal]], backend,
                gouraud_bank, profile, &partitions);
        }
    } else {
        profile->texture_commands += emit.stats[0].texture_commands +
                                     emit.stats[1].texture_commands;
        profile->triangles_emitted += emit.stats[0].triangles_emitted +
                                      emit.stats[1].triangles_emitted;
        profile->triangles_vdp1_emitted += emit.stats[0].triangles_emitted +
                                           emit.stats[1].triangles_emitted;
        profile->gouraud_bank_overflow += emit.stats[0].gouraud_bank_overflow +
                                          emit.stats[1].gouraud_bank_overflow;
    }
    /* Mario is currently a flat-material actor pass. It deliberately consumes
     * the live bridge pose now; textured actor tiles and painter interleave
     * remain separate fidelity work, rather than hiding the actor seam. */
    demo_emit_mario(snapshot, pose, backend, gouraud_bank, &partitions,
                    profile);
    if (sm64_saturn_gouraud_bank_used_bytes(gouraud_bank) > 0U) {
        saturn_dma_queue_transfer_wait(
            (void *)gouraud_bank->vram_base, gouraud_bank->staging,
            sm64_saturn_gouraud_bank_used_bytes(gouraud_bank),
            SATURN_DMA_QUEUE_SCU);
    }
    sm64_saturn_vdp1_backend_finish(backend);
#if SATURN_SLAVE_RENDER && defined(SM64_SATURN_VDP1_LWRAM_STAGING) && \
    defined(SATURN_SLAVE_VDP1_UPLOAD) && SATURN_SLAVE_VDP1_UPLOAD
    sm64_saturn_dual_worker_stats_t upload_stats;
    demo_upload_vdp1_dual(backend, &upload_stats);
    profile->slave_jobs_completed += upload_stats.slave_jobs_completed;
    profile->slave_busy_ticks += upload_stats.slave_busy_ticks;
    profile->master_wait_ticks += upload_stats.master_wait_ticks;
    profile->slave_timeouts += upload_stats.slave_timeouts;
#else
    sm64_saturn_vdp1_backend_upload(backend);
#endif
    profile->frame_serial++;
}
