#include "saturn_demo_render.h"

#ifndef SATURN_DEMO_BSP_FRAGMENT_FLAT
#define SATURN_DEMO_BSP_FRAGMENT_FLAT 0
#endif

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#if defined(__sh__)
#include <cpu/cache.h>
#endif

#include "game/camera.h"
#include "saturn_gouraud.h"
#include "saturn_ir_texture.h"
#include "saturn_ir_transform.h"
#include "saturn_matrix_kernels.h"
#include "saturn_terrain_command_template.h"
#include "saturn_terrain_emit_policy.h"
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
#define sm64_saturn_bob_lod_mid_mask sm64_saturn_bob_fragment_lod_mid_mask
#define sm64_saturn_bob_lod_far_mask sm64_saturn_bob_fragment_lod_far_mask
#endif
#include "saturn_mario_actor_mesh.h"
#if defined(SATURN_DEMO_MARIO_TEXTURES)
#include "mario_eye_uv_tiles.h"
#endif
#include "../gpl/slavedriver_dma_queue.h"
#include "../gpl/slavedriver_dual_worker.h"
#include "../gpl/slavedriver_terrain_clip.h"
#include "../gpl/slavedriver_terrain_result.h"
#include "../gpl/slavedriver_terrain_worker.h"
#include "../gpl/ztreme_frustum.h"
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
#define DEMO_TERRAIN_RESULT_CAPACITY \
    (SM64_SATURN_BOB_PRIMITIVE_COUNT * 2U)
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
#define DEMO_LOD_MID_ENTER_DEPTH 4096
#define DEMO_LOD_MID_EXIT_DEPTH 3584
#define DEMO_LOD_FAR_ENTER_DEPTH 7168
#define DEMO_LOD_FAR_EXIT_DEPTH 6144
#if SATURN_DEMO_BSP_FRAGMENTS
#define DEMO_FRAGMENT_CACHE __attribute__((section(".lwram_bss")))
#else
#define DEMO_FRAGMENT_CACHE
#endif
#define DEMO_TERRAIN_TRANSFORM_CACHE __attribute__((section(".lwram_bss")))

/* Shared transform-once cache. Both SH-2s write disjoint position indices,
 * cross the uncached phase fence below, purge their own caches, then read
 * the complete bank while producing disjoint compact-result spans.
 *
 * CORRECTED PREMISE (Pipe 5 root cause): an earlier version of this comment
 * claimed LWRAM "does not require a full cache purge". That was false --
 * LWRAM at 0x00200000 is CACHEABLE on the SH-2 exactly like HWRAM; only the
 * 0x2xxxxxxx mirror bypasses the cache. Cross-CPU reads of this bank
 * through cached aliases consumed stale lines, which is why the post-fence
 * cpu_cache_purge() in demo_transform_owned_positions() exists. This adapts
 * SlaveDriver's transform-shared-wall-vertices-once pattern INCLUDING its
 * per-pass cache flush (WALLS.C:1806-1810), and Z-Treme's shared full/LOD
 * pntbl ownership. */
static sm64_saturn_vec3i_t s_view[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static sm64_saturn_projected_vertex_t s_projected[
    SM64_SATURN_BOB_POSITION_COUNT] DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_valid[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_use_mask[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_owner[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
/* Cross-CPU fence flags and result-span headers MUST live in the uncached
 * partition (.uncached, addresses >= 0x20000000). The SH7604 has no
 * inter-CPU cache coherency: a cached-alias poll spins on the reader's own
 * stale line (it wrote the 0 reset itself), and a cached-alias span header
 * read at the join drops the slave's entire record span. This was the
 * Pipe 5 root cause -- both Mario and terrain vanished from one incoherent
 * header (stale count = slave terrain dropped; post-eviction fresh count =
 * arena overflow starving Mario's reservation). SlaveDriver's discipline is
 * a full cache flush per slave pass (WALLS.C:1806-1810) plus cache-through
 * result writes (WALLS.C:1272); tools/saturn/verify_dual_cpu_coherency.py
 * pins these placements at build time. */
#define DEMO_CROSS_CPU_SHARED __attribute__((section(".uncached")))
static volatile uint16_t s_transform_phase_ready[2]
    DEMO_CROSS_CPU_SHARED;
static volatile uint16_t s_transform_phase_failed
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_terrain_result_spans_t s_terrain_spans_shared
    DEMO_CROSS_CPU_SHARED;
static uint16_t s_bucket_counts[DEMO_BUCKETS];
static uint16_t s_bucket_offsets[DEMO_BUCKETS + 1U];
static uint16_t s_emit_order[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_emit_reordered[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_emit_count;
static uint8_t s_bsp_seen[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_spatial_admitted[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_spatial_seen[SM64_SATURN_BOB_PRIMITIVE_COUNT];
/* The generated BSP is expected to be a tree, but the runtime must not turn
 * a malformed/self-referential bake into unbounded recursion.  Z-Treme's
 * fixed-capacity traversal has the same safety property: each node is visited
 * at most once per frame. */
static uint8_t s_spatial_node_seen[SM64_SATURN_BOB_BSP_NODE_COUNT];
static uint16_t s_spatial_admission_order[
    SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_spatial_admitted_count;
static uint16_t s_render_work_order[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_render_work_count;
static uint16_t s_primitive_leaf_id[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_primitive_work_weight[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_visible[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_clipped[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_recovery[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_corner_count[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_lod_tier[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_lod_transition[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_lod_suppressed[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint8_t s_primitive_lod_texture_downgraded[
    SM64_SATURN_BOB_PRIMITIVE_COUNT];
static sm64_saturn_projected_vertex_t s_clipped_projected[
    SM64_SATURN_BOB_PRIMITIVE_COUNT][5]
    __attribute__((section(".lwram_bss")));
static uint8_t s_primitive_buckets[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static int32_t s_primitive_depth[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static uint16_t s_primitive_slots[SM64_SATURN_BOB_PRIMITIVE_COUNT];
static sm64_saturn_terrain_result_t s_terrain_master_results[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
static sm64_saturn_terrain_result_t s_terrain_slave_results[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
typedef struct demo_terrain_emit_ref {
    const sm64_saturn_terrain_result_t *record;
    uint32_t sort_key;
} demo_terrain_emit_ref_t;
static demo_terrain_emit_ref_t s_terrain_emit_refs[
    DEMO_TERRAIN_RESULT_CAPACITY];
static demo_terrain_emit_ref_t s_terrain_emit_scratch[
    DEMO_TERRAIN_RESULT_CAPACITY];
static uint16_t s_terrain_emit_count;
static sm64_saturn_projected_vertex_t s_actor_projected[SM64_MARIO_VERTEX_COUNT];
static uint8_t s_actor_valid[SM64_MARIO_VERTEX_COUNT];
static uint16_t s_actor_order[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t s_actor_slots[SM64_MARIO_PRIMITIVE_COUNT];
static uint16_t s_actor_texture_slots[SM64_MARIO_PRIMITIVE_COUNT];

static uint16_t s_actor_texture_count;
static uint16_t s_actor_command_count;
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
#define DEMO_TERRAIN_TEMPLATE_HWRAM_BUDGET 0x3710U
#define DEMO_TERRAIN_TEMPLATE_COMPACT_BYTES \
    ((SM64_SATURN_BOB_PRIMITIVE_COUNT * \
      SM64_SATURN_TERRAIN_COMPACT_ENTRY_BYTES) + \
     ((SM64_SATURN_BOB_PRIMITIVE_COUNT + 7U) / 8U))
#if DEMO_TERRAIN_TEMPLATE_COMPACT_BYTES <= DEMO_TERRAIN_TEMPLATE_HWRAM_BUDGET
#define DEMO_TERRAIN_TEMPLATE_COMPACT_ENABLED 1
#define DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY SM64_SATURN_BOB_PRIMITIVE_COUNT
#else
#define DEMO_TERRAIN_TEMPLATE_COMPACT_ENABLED 0
/* Keep types available without allocating an over-budget fragment cache. */
#define DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY 1U
#endif
/* Compact resolved VDP1 words remain in HWRAM. The immutable portable BOB
 * primitive already contains the metadata needed to derive a match, so a
 * second per-primitive metadata cache would only consume TLSF headroom. */
static sm64_saturn_terrain_resolved_command_t
    s_bob_terrain_resolved_templates[DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY];
#define DEMO_TERRAIN_TEMPLATE_VALID_BYTES \
    ((DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY + 7U) / 8U)
static uint8_t s_bob_terrain_template_valid[DEMO_TERRAIN_TEMPLATE_VALID_BYTES];
static uint8_t s_bob_terrain_templates_resolved;
static uint8_t s_bob_terrain_templates_enabled;
_Static_assert(sizeof(vdp1_cmdt_t) == SM64_SATURN_VDP1_COMMAND_BYTES,
               "terrain patcher requires the 32-byte VDP1 command format");
_Static_assert(sizeof(sm64_saturn_terrain_resolved_command_t) == 10U,
               "compact terrain template state must retain all VDP1 words");
_Static_assert(offsetof(vdp1_cmdt_t, cmd_ctrl) == 0U &&
                   offsetof(vdp1_cmdt_t, cmd_link) == 2U &&
                   offsetof(vdp1_cmdt_t, cmd_vertices) == 12U &&
                   offsetof(vdp1_cmdt_t, cmd_grda) == 28U,
               "terrain patch offsets must match vdp1_cmdt_t");
_Static_assert(sizeof(int16_vec2_t) == 2U * sizeof(int16_t) &&
                   offsetof(int16_vec2_t, x) == 0U &&
                   offsetof(int16_vec2_t, y) == sizeof(int16_t),
               "terrain patch vertices require packed x/y pairs");

static bool demo_terrain_template_valid(uint16_t primitive_index)
{
    return primitive_index < DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY &&
        (s_bob_terrain_template_valid[primitive_index >> 3U] &
         (uint8_t)(1U << (primitive_index & 7U))) != 0U;
}

static void demo_terrain_template_valid_set(uint16_t primitive_index,
                                            bool valid)
{
    if (primitive_index >= DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY)
        return;
    const uint8_t mask = (uint8_t)(1U << (primitive_index & 7U));
    uint8_t *const bits = &s_bob_terrain_template_valid[primitive_index >> 3U];
    if (valid)
        *bits |= mask;
    else
        *bits &= (uint8_t)~mask;
}
#if SATURN_DEMO_HOT_PROMOTION
/* Optional Z-Treme-style hot arena. The source bank remains the LWRAM
 * authority; these HWRAM arrays are populated once before the frame loop and
 * then become the renderer's active read-only bank. One enclosing object is
 * deliberate: Z-Treme's workarea.c pattern uses compile-time offsets rather
 * than two cursors that can collide at runtime. */
typedef struct demo_hot_workarea {
    int32_t positions[SM64_SATURN_BOB_POSITION_COUNT][3];
    sm64_saturn_bob_primitive_t primitives[SM64_SATURN_BOB_PRIMITIVE_COUNT];
} demo_hot_workarea_t;
static demo_hot_workarea_t s_bob_hot_workarea __attribute__((aligned(16)));
_Static_assert(offsetof(demo_hot_workarea_t, positions) == 0U,
               "hot positions must be the first fixed work-area region");
_Static_assert(offsetof(demo_hot_workarea_t, primitives) >=
                   sizeof(s_bob_hot_workarea.positions),
               "hot work-area regions must not overlap");
_Static_assert(sizeof(s_bob_hot_workarea) <= 65536U,
               "hot immutable BOB work area exceeds its fixed budget");
static saturn_hot_promotion_t s_bob_hot_promotion;
#endif
static const int32_t (*s_bob_positions_active)[3];
static const sm64_saturn_bob_primitive_t *s_bob_primitives_active;
static uint8_t s_bob_resident_ready;
static uint16_t s_slave_begin = SM64_SATURN_BOB_PRIMITIVE_COUNT / 2U;
static uint16_t s_last_master_wait_ticks;

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
/* The fragment bake carries its own node/reference stream. Unlike the source
 * BSP, each reference is a lowered fragment, so split polygons retain their
 * true camera dependency instead of being grouped by source primitive. */
static void demo_fragment_bsp_append(
    int16_t node, const sm64_saturn_camera_transform_t *camera)
{
    if (node < 0) return;
    const int64_t side =
        (int64_t)sm64_saturn_bob_fragment_bsp_planes[node][0] * camera->position.x +
        (int64_t)sm64_saturn_bob_fragment_bsp_planes[node][1] * camera->position.y +
        (int64_t)sm64_saturn_bob_fragment_bsp_planes[node][2] * camera->position.z +
        sm64_saturn_bob_fragment_bsp_distances[node];
    const bool camera_front = side >= 0;
    const int16_t far = sm64_saturn_bob_fragment_bsp_children[node][camera_front ? 1 : 0];
    const int16_t near = sm64_saturn_bob_fragment_bsp_children[node][camera_front ? 0 : 1];
    demo_fragment_bsp_append(far, camera);
    const uint16_t start = sm64_saturn_bob_fragment_bsp_ref_ranges[node][0];
    const uint16_t count = sm64_saturn_bob_fragment_bsp_ref_ranges[node][1];
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t primitive =
            sm64_saturn_bob_fragment_bsp_refs[start + offset];
        if (primitive >= SM64_SATURN_BOB_FRAGMENT_PRIMITIVE_COUNT ||
            s_bsp_seen[primitive] != 0U ||
            s_emit_count >= SM64_SATURN_BOB_FRAGMENT_PRIMITIVE_COUNT)
            continue;
        if (s_primitive_visible[primitive] == 0U) continue;
        s_bsp_seen[primitive] = 1U;
        s_emit_order[s_emit_count++] = primitive;
    }
    demo_fragment_bsp_append(near, camera);
}
#endif

#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
static sm64_saturn_ztreme_frustum_result_t demo_spatial_admit_node(
    int16_t node, sm64_saturn_ztreme_frustum_result_t inherited,
    const sm64_saturn_ztreme_frustum_t *frustum,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (node < 0 || node >= (int16_t)SM64_SATURN_BOB_BSP_NODE_COUNT)
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    if (s_spatial_node_seen[node] != 0U)
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    s_spatial_node_seen[node] = 1U;
    sm64_saturn_ztreme_frustum_result_t state = inherited;
    if (state != SM64_SATURN_ZTREME_FRUSTUM_INSIDE) {
        const sm64_saturn_ztreme_frustum_result_t tested =
            sm64_saturn_ztreme_frustum_aabb(
                frustum,
                sm64_saturn_bob_bsp_bounds_min[node],
                sm64_saturn_bob_bsp_bounds_max[node]);
        state = tested;
    }
    profile->demo_bob_nodes_visited++;
    if (state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) {
        profile->demo_bob_nodes_outside++;
        return state;
    }
    if (state == SM64_SATURN_ZTREME_FRUSTUM_INSIDE)
        profile->demo_bob_nodes_inside++;
    else
        profile->demo_bob_nodes_intersecting++;

    const int64_t side =
        (int64_t)sm64_saturn_bob_bsp_planes[node][0] * frustum->position[0] +
        (int64_t)sm64_saturn_bob_bsp_planes[node][1] * frustum->position[1] +
        (int64_t)sm64_saturn_bob_bsp_planes[node][2] * frustum->position[2] +
        sm64_saturn_bob_bsp_distances[node];
    const bool camera_front = side >= 0;
    const int16_t plane_near =
        sm64_saturn_bob_bsp_children[node][camera_front ? 0 : 1];
    const int16_t plane_far =
        sm64_saturn_bob_bsp_children[node][camera_front ? 1 : 0];
    const int32_t center_x =
        (sm64_saturn_bob_bsp_bounds_min[node][0] +
         sm64_saturn_bob_bsp_bounds_max[node][0]) / 2;
    const int32_t center_y =
        (sm64_saturn_bob_bsp_bounds_min[node][1] +
         sm64_saturn_bob_bsp_bounds_max[node][1]) / 2;
    const int32_t center_z =
        (sm64_saturn_bob_bsp_bounds_min[node][2] +
         sm64_saturn_bob_bsp_bounds_max[node][2]) / 2;
    const uint8_t octant =
        (frustum->position[0] >= center_x ? 1U : 0U) |
        (frustum->position[1] >= center_y ? 2U : 0U) |
        (frustum->position[2] >= center_z ? 4U : 0U);
    int16_t ordered_near = plane_near;
    int16_t ordered_far = plane_far;
    const int16_t baked_first =
        sm64_saturn_bob_bsp_octant_child_order[node][octant][0];
    const int16_t baked_second =
        sm64_saturn_bob_bsp_octant_child_order[node][octant][1];
    /* The baked order is advisory; the exact runtime plane sign remains the
     * authority. Normalize the baked pair to that near/far truth. */
    if ((baked_first == plane_near || baked_first == plane_far) &&
        (baked_second == plane_near || baked_second == plane_far) &&
        baked_first != baked_second) {
        if (baked_first == plane_near) {
            ordered_near = baked_first;
            ordered_far = baked_second;
        } else {
            ordered_near = baked_second;
            ordered_far = baked_first;
        }
    }
    demo_spatial_admit_node(ordered_near, state, frustum, profile);

    /* Local refs are admitted after the near child and before the far child,
     * which gives overflow a deterministic near-to-far survival order. */
    const uint16_t start = sm64_saturn_bob_bsp_ref_ranges[node][0];
    const uint16_t count = sm64_saturn_bob_bsp_ref_ranges[node][1];
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t primitive = sm64_saturn_bob_bsp_refs[start + offset];
        if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT ||
            s_spatial_seen[primitive] != 0U)
            continue;
        s_spatial_seen[primitive] = 1U;
        if (s_spatial_admitted_count < SM64_SATURN_BOB_PRIMITIVE_COUNT) {
            s_spatial_admitted[primitive] = 1U;
            s_spatial_admission_order[s_spatial_admitted_count++] = primitive;
            profile->demo_bob_primitives_spatial_admitted++;
        } else {
            profile->demo_bob_primitives_spatial_dropped++;
        }
    }

    demo_spatial_admit_node(ordered_far, state, frustum, profile);
    return state;
}

static void demo_spatial_admit(
    const sm64_saturn_camera_transform_t *camera,
    sm64_saturn_fast3d_profile_t *profile)
{
    memset(s_spatial_admitted, 0, sizeof(s_spatial_admitted));
    memset(s_spatial_seen, 0, sizeof(s_spatial_seen));
    memset(s_spatial_node_seen, 0, sizeof(s_spatial_node_seen));
    memset(s_spatial_admission_order, 0, sizeof(s_spatial_admission_order));
    s_spatial_admitted_count = 0U;
    const sm64_saturn_ztreme_frustum_t frustum = {
        .position = {camera->position.x, camera->position.y, camera->position.z},
        .right = {camera->right.x, camera->right.y, camera->right.z},
        .up = {camera->up.x, camera->up.y, camera->up.z},
        .forward = {camera->forward.x, camera->forward.y, camera->forward.z},
        .near_depth = SATURN_DEMO_NEAR_DEPTH,
        .far_depth = DEMO_FAR_DEPTH,
        .half_width = DEMO_CENTER_X,
        .half_height = DEMO_CENTER_Y,
        .focal_length = DEMO_FOCAL_LENGTH};
    (void)demo_spatial_admit_node(
        0, SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS, &frustum, profile);
}
#endif

static void demo_prepare_render_work_order(void)
{
    s_render_work_count = 0U;
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    for (uint16_t i = 0U; i < s_spatial_admitted_count; i++) {
        s_render_work_order[s_render_work_count++] =
            s_spatial_admission_order[i];
    }
#else
    for (uint16_t i = 0U; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++)
        s_render_work_order[s_render_work_count++] = i;
#endif
}

static uint16_t demo_choose_work_split(void)
{
    const uint16_t count = s_render_work_count;
    if (count < 2U) return count;
    uint32_t total = 0U;
    for (uint16_t work = 0U; work < count; work++) {
        const uint16_t primitive = s_render_work_order[work];
        total += s_primitive_work_weight[primitive];
    }
    const uint32_t target = total / 2U;
    uint32_t accumulated = 0U;
    uint16_t split = count / 2U;
    for (uint16_t work = 0U; work < count; work++) {
        accumulated += s_primitive_work_weight[s_render_work_order[work]];
        if (accumulated >= target) {
            split = (uint16_t)(work + 1U);
            break;
        }
    }
    const uint16_t minimum = count < 64U ? 1U : 32U;
    if (split < minimum) split = minimum;
    if (split > count - minimum) split = (uint16_t)(count - minimum);
    /* SlaveDriver's prior-spin correction is bounded to a few work units per
     * frame. A high master wait means the slave needs less work; a low wait
     * lets it claim a little more on the next frame. */
    if (s_last_master_wait_ticks > 100U &&
        split + 8U < (uint16_t)(count - minimum))
        split = (uint16_t)(split + 8U);
    else if (s_last_master_wait_ticks < 20U && split > minimum + 8U)
        split = (uint16_t)(split - 8U);
    return split;
}

static void demo_prepare_position_owners(uint16_t split, bool dual_phase)
{
    memset(s_position_use_mask, 0, sizeof(s_position_use_mask));
    memset(s_position_owner, UINT8_MAX, sizeof(s_position_owner));

    for (uint16_t work = 0U; work < s_render_work_count; work++) {
        const uint16_t primitive_index = s_render_work_order[work];
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[primitive_index];
        const uint8_t lane_mask =
            dual_phase && work >= split ? 2U : 1U;
        /* Results and VDP1 distorted sprites always carry four corners.
         * Generated triangles repeat C in slot 3, so include that index in
         * the shared bank even when the source primitive has three corners. */
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const uint16_t position = primitive->indices[corner];
            if (position < SM64_SATURN_BOB_POSITION_COUNT)
                s_position_use_mask[position] |= lane_mask;
        }
    }

    uint16_t owned[2] = {0U, 0U};
    for (uint16_t position = 0U;
         position < SM64_SATURN_BOB_POSITION_COUNT; position++) {
        const uint8_t use_mask = s_position_use_mask[position];
        if (use_mask == 0U) continue;
        uint8_t owner;
        if (!dual_phase || use_mask == 1U)
            owner = 0U;
        else if (use_mask == 2U)
            owner = 1U;
        else
            owner = owned[0] <= owned[1] ? 0U : 1U;
        s_position_owner[position] = owner;
        owned[owner]++;
    }
}

static void __attribute__((unused)) demo_build_clipped_quad(
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

typedef struct demo_mario_transform_context {
    const sm64_saturn_ir_transform_job_t *job;
    const sm64_saturn_mario_actor_snapshot_t *snapshot;
    const sm64_saturn_mario_actor_pose_t *pose;
} demo_mario_transform_context_t;

typedef struct demo_classify_context {
    const sm64_saturn_bob_primitive_t *primitives;
    const uint16_t *work_order;
    const sm64_saturn_camera_transform_t *camera;
    const sm64_saturn_ir_transform_job_t *job;
    uint32_t visible[2];
    uint32_t transformed[2];
    uint32_t radius_rejected[2];
    uint32_t near_rejected[2];
    uint32_t degenerate[2];
    uint32_t clip_away[2];
    uint32_t clip_to_one[2];
    uint32_t clip_to_two[2];
    uint32_t clip_recovery[2];
    uint32_t clip_overflow[2];
    bool dual_phase;
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
    sm64_saturn_gouraud_table_t *const *gouraud_tables;
    const uintptr_t *gouraud_addresses;
    demo_emit_stats_t stats[2];
} demo_emit_context_t;

static void demo_emit_mario_range(void *opaque, uint16_t begin,
                                  uint16_t end);

/* Z-Treme's hysteretic LOD boundary is applied after transform, when the
 * primitive's view-space depth is known.  All tiers share the same promoted
 * positions; only the generated primitive masks and texture binding policy
 * change. */
static uint8_t demo_lod_select(uint16_t primitive_index, int32_t depth)
{
    uint8_t previous = s_primitive_lod_tier[primitive_index];
    uint8_t next = previous;
#if SATURN_DEMO_POLY_TIER == 0
    next = 0U;
#else
    if (previous == 0U) {
        if (depth >= DEMO_LOD_FAR_ENTER_DEPTH) next = 2U;
        else if (depth >= DEMO_LOD_MID_ENTER_DEPTH) next = 1U;
    } else if (previous == 1U) {
        if (depth >= DEMO_LOD_FAR_ENTER_DEPTH) next = 2U;
        else if (depth < DEMO_LOD_MID_EXIT_DEPTH) next = 0U;
    } else {
        if (depth < DEMO_LOD_FAR_EXIT_DEPTH) {
            next = depth >= DEMO_LOD_MID_ENTER_DEPTH ? 1U : 0U;
        }
    }
#endif
    s_primitive_lod_tier[primitive_index] = next;
    s_primitive_lod_transition[primitive_index] = previous != next ? 1U : 0U;
    return next;
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

static void demo_build_primitive_work_metadata(void)
{
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++) {
        s_primitive_leaf_id[primitive] = UINT16_MAX;
        s_primitive_work_weight[primitive] = 1U;
    }
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    for (uint16_t node = 0U; node < SM64_SATURN_BOB_BSP_NODE_COUNT; node++) {
        const int16_t start = sm64_saturn_bob_bsp_leaf_ranges[node][0];
        const int16_t count = sm64_saturn_bob_bsp_leaf_ranges[node][1];
        if (start < 0 || count <= 0) continue;
        const uint16_t baked_weight = sm64_saturn_bob_bsp_work_weight[node];
        const uint16_t per_primitive =
            baked_weight / (uint16_t)count == 0U
                ? 1U : baked_weight / (uint16_t)count;
        for (int16_t offset = 0; offset < count; offset++) {
            const uint16_t ref = (uint16_t)(start + offset);
            const uint16_t primitive = sm64_saturn_bob_bsp_refs[ref];
            if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;
            if (s_primitive_leaf_id[primitive] != UINT16_MAX) continue;
            s_primitive_leaf_id[primitive] = node;
            s_primitive_work_weight[primitive] = per_primitive;
        }
    }
#endif
}

void sm64_saturn_demo_render_init(void)
{
    memset(s_primitive_lod_tier, 0, sizeof(s_primitive_lod_tier));
    memset(s_primitive_lod_transition, 0,
           sizeof(s_primitive_lod_transition));
    memset(s_primitive_lod_suppressed, 0,
           sizeof(s_primitive_lod_suppressed));
    memset(s_primitive_lod_texture_downgraded, 0,
           sizeof(s_primitive_lod_texture_downgraded));
    memcpy(s_bob_positions_resident, sm64_saturn_bob_positions,
           sizeof(s_bob_positions_resident));
    memcpy(s_bob_primitives_resident, sm64_saturn_bob_primitives,
           sizeof(s_bob_primitives_resident));
    s_bob_positions_active = s_bob_positions_resident;
    s_bob_primitives_active = s_bob_primitives_resident;
#if SATURN_DEMO_HOT_PROMOTION
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_hot_workarea.positions,
        sizeof(s_bob_hot_workarea.positions));
    const int32_t (*hot_positions)[3] = saturn_hot_promote(
        &s_bob_hot_promotion, s_bob_positions_resident,
        sizeof(s_bob_positions_resident), 16U);
    /* Reset to the second compile-time region; the enclosing work-area
     * assertions prove that this cannot overlap the position bank. */
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_hot_workarea.primitives,
        sizeof(s_bob_hot_workarea.primitives));
    const sm64_saturn_bob_primitive_t *hot_primitives = saturn_hot_promote(
        &s_bob_hot_promotion, s_bob_primitives_resident,
        sizeof(s_bob_primitives_resident), 16U);
    if (hot_positions != NULL && hot_primitives != NULL) {
        s_bob_positions_active = hot_positions;
        s_bob_primitives_active = hot_primitives;
    }
#endif
    s_bob_terrain_templates_enabled =
        sm64_saturn_terrain_compact_cache_fits(
            SM64_SATURN_BOB_PRIMITIVE_COUNT,
            DEMO_TERRAIN_TEMPLATE_HWRAM_BUDGET) &&
        DEMO_TERRAIN_TEMPLATE_COMPACT_ENABLED;
    if (s_bob_terrain_templates_enabled != 0U) {
        for (uint16_t primitive_index = 0U;
             primitive_index < SM64_SATURN_BOB_PRIMITIVE_COUNT;
             primitive_index++) {
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[primitive_index];
        sm64_saturn_terrain_command_template_t metadata;
        demo_terrain_template_valid_set(
            primitive_index, sm64_saturn_terrain_template_build_from_bob(
                &metadata, primitive->textured != 0U, primitive->rgb,
                primitive->tile_offset));
        }
    }
    s_bob_terrain_templates_resolved = 0U;
    demo_build_primitive_work_metadata();
    s_bob_resident_ready = 1U;
}

static sm64_saturn_camera_transform_t demo_camera(
    const sm64_saturn_mario_actor_snapshot_t *snapshot)
{
    const sm64_saturn_vec3i_t position = {
        snapshot->camera_position[0],
        snapshot->camera_position[1],
        snapshot->camera_position[2]
    };
    const sm64_saturn_vec3i_t focus = {
        snapshot->camera_focus[0],
        snapshot->camera_focus[1],
        snapshot->camera_focus[2]
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

static bool __attribute__((unused)) demo_primitive_in_radius(
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

static bool demo_transform_owned_positions(
    demo_classify_context_t *context, uint8_t lane)
{
    if (context == NULL || context->job == NULL || lane > 1U ||
        s_transform_phase_failed != 0U)
        return false;

    for (uint16_t position = 0U;
         position < SM64_SATURN_BOB_POSITION_COUNT; position++) {
        if (s_position_owner[position] != lane) continue;
        if ((position % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled()) {
            s_transform_phase_failed = 1U;
            return false;
        }
        const int32_t *world = s_bob_positions_active[position];
        const bool valid = sm64_saturn_ir_transform_one(
            context->job,
            (sm64_saturn_vec3i_t){world[0], world[1], world[2]},
            &s_view[position], &s_projected[position]);
        s_position_valid[position] = valid ? 1U : 0U;
        context->transformed[lane]++;
    }

    /* Both CPUs write disjoint LWRAM indices, then cross this small phase
     * fence before either consumes the shared transformed bank. This is the
     * same transform-once/index-many ownership used by SlaveDriver's wall
     * vertices and Z-Treme's shared full/LOD point table. */
    __asm__ volatile("" ::: "memory");
    s_transform_phase_ready[lane] = 1U;
    if (!context->dual_phase) return true;

    uint32_t spins = 0U;
    const uint8_t peer = lane ^ 1U;
    while (s_transform_phase_ready[peer] == 0U &&
           s_transform_phase_failed == 0U &&
           spins++ < 4000000U) {
        if ((spins & 0x3FFFU) == 0U &&
            sm64_saturn_dual_worker_cancelled()) {
            s_transform_phase_failed = 1U;
            break;
        }
    }
    if (s_transform_phase_ready[peer] == 0U) {
        s_transform_phase_failed = 1U;
        return false;
    }
    __asm__ volatile("" ::: "memory");
#if defined(__sh__)
    /* Each CPU drops its cached view of the shared transform bank before
     * consuming the peer's half. The peer's write-through stores reached
     * LWRAM, but this CPU's cache may hold pre-fence lines for those
     * indices. One whole-cache purge per CPU per frame is SlaveDriver's own
     * discipline at every slave pass boundary (WALLS.C:1806-1810); the
     * SH7604 cache is write-through, so purge is invalidate-only and there
     * is nothing dirty to lose. */
    cpu_cache_purge();
#endif
    return true;
}

static void demo_classify_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_classify_context_t *context = opaque;
#if !(SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS)
    const sm64_saturn_camera_transform_t *camera = context->camera;
#endif
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t work = begin; work < end; work++) {
        if (((uint16_t)(work - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const uint16_t i = context->work_order[work];
        const sm64_saturn_bob_primitive_t *primitive =
            &context->primitives[i];
        s_primitive_visible[i] = 0U;
        s_primitive_clipped[i] = 0U;
        s_primitive_recovery[i] = 0U;
        s_primitive_corner_count[i] =
            primitive->source1 == 0xFFFFU ? 3U : 4U;
        bool spatial_rejected = false;
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
        spatial_rejected = s_spatial_admitted[i] == 0U;
#else
        spatial_rejected = !demo_primitive_in_radius(primitive, camera);
#endif
        if (spatial_rejected) {
            context->radius_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        /* The shared position bank was transformed once before this phase.
         * Faces only validate and index it, as in SlaveDriver's WALLS.C wall
         * path and Z-Treme's full/LOD meshes sharing one pntbl. */
        bool transform_valid = true;
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const uint16_t index = primitive->indices[corner];
            if (index >= SM64_SATURN_BOB_POSITION_COUNT ||
                s_position_valid[index] == 0U)
                transform_valid = false;
        }
        if (!transform_valid) {
            context->near_rejected[lane]++;
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
        if (!any_front) {
            context->near_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        int32_t depth = s_view[primitive->indices[0]].z;
        for (uint8_t corner = 1U; corner < 4U; corner++)
            if (s_view[primitive->indices[corner]].z > depth)
                depth = s_view[primitive->indices[corner]].z;
        const uint8_t lod_tier = demo_lod_select(i, depth);
        s_primitive_lod_suppressed[i] = 0U;
        s_primitive_lod_texture_downgraded[i] = 0U;
        /* The far mask is baked from stable source identity. Preserve the
         * route-critical prefix in every build; only optional tier-2 builds
         * remove the conservative one-in-eight far subset. */
        if (SATURN_DEMO_POLY_TIER >= 2 && lod_tier >= 2U &&
            sm64_saturn_bob_lod_far_mask[i] == 0U &&
            primitive->source0 >= 128U) {
            s_primitive_lod_suppressed[i] = 1U;
            s_primitive_visible[i] = 0U;
            continue;
        }
        if (lod_tier != 0U && primitive->textured != 0U &&
            primitive->tile_size > 16U) {
            s_primitive_lod_texture_downgraded[i] = 1U;
        }
        bool any_back = false;
        sm64_saturn_terrain_clip_vertex_t clip_input[4];
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const sm64_saturn_vec3i_t view =
                s_view[primitive->indices[corner]];
            clip_input[corner] = (sm64_saturn_terrain_clip_vertex_t){
                .view = view,
                .shade = (uint16_t)(((uint16_t)primitive->rgb[0] << 10) |
                                    ((uint16_t)primitive->rgb[1] << 5) |
                                    primitive->rgb[2]),
                .source_edge = corner,
                .source_corner = corner,
                .edge_t_q16 = 0};
            if (view.z < SATURN_DEMO_NEAR_DEPTH) any_back = true;
        }
        if (any_back && context->job->clip_near) {
            sm64_saturn_terrain_clip_output_t clipped;
            const int clipped_count = sm64_saturn_terrain_clip_near_quad(
                clip_input, SATURN_DEMO_NEAR_DEPTH, &clipped);
            if (clipped_count == 0) {
                if (clipped.classification == SM64_SATURN_TERRAIN_CLIP_AWAY)
                    context->clip_away[lane]++;
                else
                    context->clip_overflow[lane]++;
                context->near_rejected[lane]++;
                s_primitive_visible[i] = 0U;
                continue;
            }
            if (clipped_count < 3) {
                context->clip_to_one[lane]++;
                context->near_rejected[lane]++;
                s_primitive_visible[i] = 0U;
                continue;
            }
            if (clipped_count == 3) context->clip_to_one[lane]++;
            else if (clipped_count == 4) context->clip_to_two[lane]++;
            else if (clipped_count == 5)
                context->clip_recovery[lane]++;
            else {
                context->clip_overflow[lane]++;
                context->near_rejected[lane]++;
                continue;
            }
            /* Preserve the source material across the software near-plane
             * split.  Mapping the complete source tile over the clipped
             * distorted sprite can stretch its edge, but changing the same
             * surface to a flat recovery material as it crosses the plane is
             * far more visible: it makes terrain textures blink with camera
             * motion.  Z-Treme keeps textured ATTRs under UseNearClip
             * (ZT_LOAD_MODEL.c:88-102), while SlaveDriver confines its flat
             * drawClippedFace recovery to a bounded exceptional path
             * (WALLS.C:723-803).  A future UV-domain split can improve the
             * clipped edge without violating this material-stability rule. */
            s_primitive_clipped[i] = 1U;
            const uint8_t projected_count = (uint8_t)clipped_count;
            s_primitive_corner_count[i] = projected_count;
            bool clip_projection_valid = true;
            for (uint8_t corner = 0U; corner < projected_count; corner++) {
                if (!sm64_saturn_ir_project_view(
                        context->job, clipped.vertices[corner].view,
                        &s_clipped_projected[i][corner])) {
                    clip_projection_valid = false;
                    context->near_rejected[lane]++;
                    break;
                }
            }
            if (!clip_projection_valid) continue;
            if (projected_count == 3U)
                s_clipped_projected[i][3] = s_clipped_projected[i][2];
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
        const sm64_saturn_projected_vertex_t *area_vertices =
            s_primitive_clipped[i] != 0U
                ? s_clipped_projected[i] : NULL;
        const uint8_t area_count = s_primitive_clipped[i] != 0U
            ? s_primitive_corner_count[i] : 4U;
        bool has_area = false;
        for (uint8_t fan = 1U; fan + 1U < area_count; fan++) {
            const sm64_saturn_projected_vertex_t a = area_vertices != NULL
                ? area_vertices[0] : s_projected[primitive->indices[0]];
            const sm64_saturn_projected_vertex_t b = area_vertices != NULL
                ? area_vertices[fan] :
                  s_projected[primitive->indices[fan]];
            const sm64_saturn_projected_vertex_t c = area_vertices != NULL
                ? area_vertices[fan + 1U] :
                  s_projected[primitive->indices[fan + 1U]];
            const int32_t cross =
                (int32_t)(b.x - a.x) * (c.y - a.y) -
                (int32_t)(b.y - a.y) * (c.x - a.x);
            if (cross != 0) {
                has_area = true;
                break;
            }
        }
        if (!has_area) {
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

typedef struct demo_terrain_compact_context {
    demo_classify_context_t *classify;
    sm64_saturn_terrain_result_spans_t *spans;
} demo_terrain_compact_context_t;

/* One coarse same-frame callback. Each SH-2 transforms its owned subset of
 * the shared indexed position bank, crosses one phase fence, then classifies
 * and compacts its disjoint primitive range. This deliberately follows
 * SlaveDriver's coarse split/join rather than dispatching per face. */
static void demo_terrain_compact_range(void *opaque, uint16_t begin,
                                       uint16_t end)
{
    demo_terrain_compact_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    if (!demo_transform_owned_positions(context->classify, lane)) return;
    demo_classify_range(context->classify, begin, end);
    sm64_saturn_terrain_result_arena_t *arena = lane == 0U
        ? &context->spans->master : &context->spans->slave;
    for (uint16_t work = begin; work < end; work++) {
        const uint16_t primitive_index = context->classify->work_order[work];
        if (s_primitive_visible[primitive_index] == 0U) continue;
        const sm64_saturn_bob_primitive_t *primitive =
            &context->classify->primitives[primitive_index];
        const sm64_saturn_projected_vertex_t *projected =
            s_primitive_clipped[primitive_index] != 0U
                ? s_clipped_projected[primitive_index] : NULL;
        const uint8_t clipped_count =
            s_primitive_corner_count[primitive_index];
        const uint8_t result_count = clipped_count == 5U ? 2U : 1U;
        sm64_saturn_terrain_result_t *results = NULL;
        if (!sm64_saturn_terrain_result_reserve(
                arena, result_count, &results))
            continue;
        const uint16_t flags = SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            (primitive->textured != 0U &&
             s_primitive_lod_texture_downgraded[primitive_index] == 0U
                ? SM64_SATURN_TERRAIN_RESULT_TEXTURED :
                  SM64_SATURN_TERRAIN_RESULT_GOURAUD) |
            (s_primitive_lod_texture_downgraded[primitive_index] != 0U
                ? SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED : 0U) |
            (s_primitive_clipped[primitive_index] != 0U
                ? SM64_SATURN_TERRAIN_RESULT_CLIPPED : 0U) |
            (s_primitive_recovery[primitive_index] != 0U
                ? SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL : 0U);
        const uint16_t shade = (uint16_t)(
            ((uint16_t)primitive->rgb[0] << 10) |
            ((uint16_t)primitive->rgb[1] << 5) | primitive->rgb[2]);
        for (uint8_t fragment = 0U; fragment < result_count; fragment++) {
            sm64_saturn_terrain_result_t *result = &results[fragment];
            const uint8_t result_corners =
                fragment == 1U ? 3U :
                (clipped_count < 4U ? clipped_count : 4U);
            for (uint8_t corner = 0U; corner < 4U; corner++) {
                uint8_t source_corner;
                if (fragment == 1U) {
                    static const uint8_t fan_tail[4] = {0U, 3U, 4U, 4U};
                    source_corner = fan_tail[corner];
                } else {
                    source_corner = corner < result_corners
                        ? corner : (uint8_t)(result_corners - 1U);
                }
                result->corners[corner] = projected != NULL
                    ? projected[source_corner]
                    : s_projected[primitive->indices[source_corner]];
                result->gouraud[corner] = shade;
            }
            result->primitive_id = primitive_index;
            result->leaf_id = s_primitive_leaf_id[primitive_index];
            result->material_id = primitive->source0;
            result->texture_slot = primitive->tile_offset > UINT16_MAX
                ? UINT16_MAX : (uint16_t)primitive->tile_offset;
            result->painter_key =
                (uint32_t)s_primitive_depth[primitive_index];
            result->flags = flags;
            result->corner_count = result_corners;
            result->clip_class =
                s_primitive_clipped[primitive_index] != 0U
                    ? SM64_SATURN_TERRAIN_CLIP_CROSSES
                    : SM64_SATURN_TERRAIN_CLIP_FRONT;
            (void)sm64_saturn_terrain_result_commit(arena, result);
        }
    }
}

static void demo_merge_terrain_results(
    const sm64_saturn_terrain_result_spans_t *spans)
{
    if (spans == NULL) {
        s_terrain_emit_count = 0U;
        return;
    }
    const sm64_saturn_terrain_result_t *master_records =
        sm64_saturn_terrain_result_uncached_records(spans->master.records);
    const sm64_saturn_terrain_result_t *slave_records =
        sm64_saturn_terrain_result_uncached_records(spans->slave.records);
    /* BOB's source BSP does not split crossing polygons, so tree traversal is
     * not a complete painter order. Z-Treme retains per-polygon SORT_MAX/MIN
     * depth policy, and SlaveDriver sorts visible leaves by distance and cut
     * planes before dispatch (WALLS.C:1986-2052, 2180-2232). Keep spatial
     * admission from the bake, but restore a stable far-to-near result sort.
     *
     * Only a cached pointer/key descriptor moves: compact LWRAM records are
     * not copied, and each uncached painter key is read once after the join. */
    uint16_t unsorted_count = 0U;
    for (uint16_t i = 0U; i < spans->master.count &&
         unsorted_count < DEMO_TERRAIN_RESULT_CAPACITY; i++) {
        const sm64_saturn_terrain_result_t *record = &master_records[i];
        const uint32_t depth = record->painter_key > UINT16_MAX
            ? UINT16_MAX : record->painter_key;
        s_terrain_emit_refs[unsorted_count++] = (demo_terrain_emit_ref_t){
            .record = record,
            .sort_key = (depth << 16) |
                (uint16_t)(UINT16_MAX - record->primitive_id)};
    }
    for (uint16_t i = 0U; i < spans->slave.count &&
         unsorted_count < DEMO_TERRAIN_RESULT_CAPACITY; i++) {
        const sm64_saturn_terrain_result_t *record = &slave_records[i];
        const uint32_t depth = record->painter_key > UINT16_MAX
            ? UINT16_MAX : record->painter_key;
        s_terrain_emit_refs[unsorted_count++] = (demo_terrain_emit_ref_t){
            .record = record,
            .sort_key = (depth << 16) |
                (uint16_t)(UINT16_MAX - record->primitive_id)};
    }
    demo_terrain_emit_ref_t *src = s_terrain_emit_refs;
    demo_terrain_emit_ref_t *dst = s_terrain_emit_scratch;
    for (uint16_t width = 1U; width < unsorted_count; width <<= 1U) {
        for (uint16_t start = 0U; start < unsorted_count;
             start = (uint16_t)(start + (uint16_t)(width << 1U))) {
            const uint16_t mid =
                (uint16_t)((start + width < unsorted_count) ?
                    start + width : unsorted_count);
            const uint16_t end =
                (uint16_t)((mid + width < unsorted_count) ?
                    mid + width : unsorted_count);
            uint16_t left = start;
                uint16_t right = mid;
            for (uint16_t out = start; out < end; out++) {
                bool take_right = left >= mid;
                if (right < end && !take_right &&
                    src[right].sort_key > src[left].sort_key)
                    take_right = true;
                if (take_right)
                    dst[out] = src[right++];
                else
                    dst[out] = src[left++];
            }
        }
        demo_terrain_emit_ref_t *swap = src;
        src = dst;
        dst = swap;
        if (width > (uint16_t)(UINT16_MAX >> 1U)) break;
    }
    if (src != s_terrain_emit_refs)
        memcpy(s_terrain_emit_refs, src,
                sizeof(s_terrain_emit_refs[0]) * unsorted_count);
    s_terrain_emit_count = unsorted_count;
}

static void __attribute__((unused)) demo_emit_primitive(
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
        /* Castleviewer and the shared bake path lower textured triangles as
         * repeated-C A/B/C/C sprites. True source quads retain their fourth
         * corner; flat triangles use the same repeated-C convention. */
        primitive->source1 == 0xFFFFU
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
    if (primitive->textured != 0U && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        s_primitive_recovery[primitive - s_bob_primitives_active] == 0U) {
        const bool bound = sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices);
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

static void demo_resolve_terrain_command_templates(
    const vdp1_vram_partitions_t *partitions)
{
    if (s_bob_terrain_templates_enabled == 0U ||
        s_bob_terrain_templates_resolved != 0U || partitions == NULL)
        return;

    const int16_vec2_t placeholder_vertices[4] = {
        INT16_VEC2_INITIALIZER(0, 0), INT16_VEC2_INITIALIZER(0, 0),
        INT16_VEC2_INITIALIZER(0, 0), INT16_VEC2_INITIALIZER(0, 0)};
    for (uint16_t primitive_index = 0U;
         primitive_index < SM64_SATURN_BOB_PRIMITIVE_COUNT;
         primitive_index++) {
        if (!demo_terrain_template_valid(primitive_index))
            continue;

        sm64_saturn_terrain_command_template_t metadata;
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[primitive_index];
        if (!sm64_saturn_terrain_template_build_from_bob(
                &metadata, primitive->textured != 0U, primitive->rgb,
                primitive->tile_offset)) {
            demo_terrain_template_valid_set(primitive_index, false);
            continue;
        }
        vdp1_cmdt_t command;
        memset(&command, 0, sizeof(command));
        switch ((sm64_saturn_shade_path_t)metadata.shade_path) {
        case SM64_SATURN_SHADE_FLAT_REPLACE:
            vdp1_cmdt_polygon_set(&command);
            vdp1_cmdt_draw_mode_set(&command, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(&command, (rgb1555_t){
                .raw = (uint16_t)(metadata.color | 0x8000U)});
            break;
        case SM64_SATURN_SHADE_GOURAUD:
            vdp1_cmdt_polygon_set(&command);
            vdp1_cmdt_draw_mode_set(&command, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_color_set(&command, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()});
            break;
        case SM64_SATURN_SHADE_TEXTURED:
            if (SATURN_DEMO_BSP_FRAGMENT_FLAT ||
                !sm64_saturn_ir_texture_bind_clut16(
                    &command, partitions, primitive->tile_offset,
                    primitive->tile_size, primitive->tile_size,
                    (uint16_t)(primitive->clut_offset /
                               sizeof(vdp1_clut_t)),
                    VDP1_CMDT_CC_REPLACE, placeholder_vertices)) {
                demo_terrain_template_valid_set(primitive_index, false);
            }
            break;
        default:
            demo_terrain_template_valid_set(primitive_index, false);
            break;
        }
        if (demo_terrain_template_valid(primitive_index)) {
            vdp1_cmdt_end_clear(&command);
            command.cmd_link = 0U;
            s_bob_terrain_resolved_templates[primitive_index] =
                (sm64_saturn_terrain_resolved_command_t){
                    .control = command.cmd_ctrl,
                    .pmod = command.cmd_pmod,
                    .colr = command.cmd_colr,
                    .srca = command.cmd_srca,
                    .size = command.cmd_size};
        }
    }
    s_bob_terrain_templates_resolved = 1U;
}

static void demo_emit_terrain_result(
    const sm64_saturn_terrain_result_t *result,
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const vdp1_vram_partitions_t *partitions)
{
    if (result == NULL || primitive == NULL ||
        !sm64_saturn_terrain_result_validate(result))
        return;
    int16_vec2_t vertices[4];
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        vertices[corner].x = result->corners[corner].x;
        vertices[corner].y = result->corners[corner].y;
    }
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        result->corner_count == 3U ? vertices[2] : vertices[3]};
    const int32_t cross = (int32_t)(vertices[1].x - vertices[0].x) *
                              (vertices[2].y - vertices[0].y) -
                          (int32_t)(vertices[1].y - vertices[0].y) *
                              (vertices[2].x - vertices[0].x);
    if (cross == 0) {
        profile->reject_degenerate++;
        return;
    }
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1U);
    if (cmdt == NULL) {
        profile->reject_vdp1_arena_capacity++;
        return;
    }
    const bool recovery =
        (result->flags & SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL) != 0U;
    const uint16_t effective_flags = recovery
        ? (uint16_t)(result->flags &
                     ~SM64_SATURN_TERRAIN_RESULT_TEXTURED)
        : result->flags;
    const sm64_saturn_shade_path_t shade_path =
        sm64_saturn_terrain_shade_path(effective_flags, result->gouraud);
    const bool textured = shade_path == SM64_SATURN_SHADE_TEXTURED;
    sm64_saturn_terrain_command_template_t metadata;
    const bool metadata_valid = sm64_saturn_terrain_template_build_from_bob(
        &metadata, primitive->textured != 0U, primitive->rgb,
        primitive->tile_offset);
    const bool template_matches =
        s_bob_terrain_templates_enabled != 0U &&
        result->primitive_id < SM64_SATURN_BOB_PRIMITIVE_COUNT &&
        demo_terrain_template_valid(result->primitive_id) &&
        metadata_valid &&
        sm64_saturn_terrain_template_matches(
            &metadata, effective_flags,
            result->gouraud, result->texture_slot);
    if (template_matches) {
        sm64_saturn_gouraud_table_t *table = NULL;
        uintptr_t gouraud_address = 0U;
        if (shade_path == SM64_SATURN_SHADE_GOURAUD)
            table = sm64_saturn_gouraud_bank_alloc(gouraud_bank,
                                                    &gouraud_address);
        const sm64_saturn_terrain_resolved_command_t *const resolved =
            &s_bob_terrain_resolved_templates[result->primitive_id];
        const bool patched = sm64_saturn_terrain_template_patch_resolved_record(
            cmdt, resolved,
            (const int16_t (*)[2])(const void *)shape_vertices,
            0U, false, table != NULL, gouraud_address);
        if (patched) {
            if (textured) {
                profile->texture_commands++;
                profile->triangles_vdp1_emitted++;
                profile->triangles_emitted++;
                return;
            }
            if (shade_path == SM64_SATURN_SHADE_FLAT_REPLACE) {
                profile->flat_primitives++;
            } else if (table != NULL) {
                for (uint8_t corner = 0U; corner < 4U; corner++)
                    table->colors[corner] =
                        (uint16_t)(result->gouraud[corner] | 0x8000U);
                profile->gouraud_primitives++;
            } else {
                vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_REPLACE});
                vdp1_cmdt_color_set(cmdt, RGB1555(
                    1, primitive->rgb[0], primitive->rgb[1],
                    primitive->rgb[2]));
                profile->gouraud_bank_overflow++;
            }
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
        demo_terrain_template_valid_set(result->primitive_id, false);
    }

    /* Per-entry fallback: dynamic LOD/recovery state may legitimately choose
     * a different material path, and an invalid texture address must never
     * poison neighboring templates. */
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (textured && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        !recovery) {
        const bool bound = sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices);
        if (bound) {
            profile->texture_commands++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
    }
    if (shade_path == SM64_SATURN_SHADE_FLAT_REPLACE) {
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = (uint16_t)(result->gouraud[0] | 0x8000U)});
        profile->flat_primitives++;
    } else {
        sm64_saturn_gouraud_table_t *table = NULL;
        uintptr_t gouraud_address = 0U;
        if (!textured || recovery)
            table = sm64_saturn_gouraud_bank_alloc(gouraud_bank,
                                                    &gouraud_address);
        if (table != NULL) {
            for (uint8_t corner = 0U; corner < 4U; corner++)
                table->colors[corner] =
                    (uint16_t)(result->gouraud[corner] | 0x8000U);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()});
            vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)gouraud_address);
            profile->gouraud_primitives++;
        } else {
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                              primitive->rgb[1], primitive->rgb[2]));
            if (!textured || recovery)
                profile->gouraud_bank_overflow++;
        }
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
static void __attribute__((unused)) demo_emit_primitive_at(
    const sm64_saturn_bob_primitive_t *primitive,
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    sm64_saturn_gouraud_table_t *gouraud_table,
    uintptr_t gouraud_address,
    demo_emit_stats_t *stats)
{
    int16_vec2_t vertices[4];
    demo_primitive_screen_vertices(primitive, vertices);
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        primitive->source1 == 0xFFFFU
            ? vertices[2] : vertices[3]
    };
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (primitive->textured != 0U && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        s_primitive_recovery[primitive - s_bob_primitives_active] == 0U &&
        sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices)) {
        stats->texture_commands++;
        stats->triangles_emitted++;
        return;
    }
    if (gouraud_table != NULL) {
        const rgb1555_t color = RGB1555(1, primitive->rgb[0],
                                        primitive->rgb[1], primitive->rgb[2]);
        gouraud_table->colors[0] = color.raw;
        gouraud_table->colors[1] = color.raw;
        gouraud_table->colors[2] = color.raw;
        gouraud_table->colors[3] = color.raw;
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
    }
    if (primitive->textured == 0U && gouraud_table == NULL)
        stats->gouraud_bank_overflow++;
    stats->triangles_emitted++;
}

static void __attribute__((unused)) demo_emit_range(void *opaque, uint16_t begin, uint16_t end)
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
                               context->partitions,
                               context->gouraud_tables[i],
                               context->gouraud_addresses[i],
                               &context->stats[lane]);
    }
}

#endif /* SATURN_SLAVE_RENDER */

static void __attribute__((unused)) demo_emit_mario_range(void *opaque, uint16_t begin, uint16_t end)
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

#if SATURN_SLAVE_RENDER && defined(SM64_SATURN_VDP1_LWRAM_STAGING)
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

static uint16_t demo_prepare_mario(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose)
{
    s_actor_draw_count = 0U;
    s_actor_texture_count = 0U;
    s_actor_command_count = 0U;
    if (snapshot == NULL || pose == NULL || !snapshot->valid ||
        pose->vertices == NULL || pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return 0U;

    s_actor_light_intensity = pose->light_intensity;
#if defined(SATURN_DEMO_MARIO_TEXTURES)
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
    s_actor_command_count =
        (uint16_t)(s_actor_draw_count + s_actor_texture_count);
#else
    for (uint16_t i = 0; i < SM64_MARIO_PRIMITIVE_COUNT; i++) {
        const uint16_t *primitive = sm64_mario_primitives[i];
        if (s_actor_valid[primitive[1]] && s_actor_valid[primitive[2]] &&
            s_actor_valid[primitive[3]] && s_actor_valid[primitive[4]])
            s_actor_command_count++;
    }
#endif
    return s_actor_command_count;
}

static void demo_reserve_mario_gouraud(
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile)
{
    memset(s_actor_gouraud, 0, sizeof(s_actor_gouraud));
    memset(s_actor_gouraud_addresses, 0, sizeof(s_actor_gouraud_addresses));
#if defined(SATURN_DEMO_MARIO_TEXTURES)
    for (uint16_t i = 0; i < s_actor_draw_count; i++) {
        s_actor_gouraud[i] = sm64_saturn_gouraud_bank_alloc(
            gouraud_bank, &s_actor_gouraud_addresses[i]);
        if (s_actor_gouraud[i] == NULL) profile->gouraud_bank_overflow++;
    }
#else
    (void)gouraud_bank;
    (void)profile;
#endif
}

static void demo_emit_mario(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    sm64_saturn_vdp1_backend_t *backend,
    const vdp1_vram_partitions_t *partitions,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid ||
        pose->vertices == NULL || pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return;
    for (uint16_t i = 0; i < SM64_MARIO_VERTEX_COUNT; i++)
        if (s_actor_valid[i]) profile->demo_actor_vertices_valid++;
#if defined(SATURN_DEMO_MARIO_TEXTURES)
    if (s_actor_draw_count != 0U &&
        sm64_saturn_vdp1_backend_reserve(
            backend, s_actor_command_count) != NULL) {
        uint16_t command_slot = (uint16_t)(backend->commands.cursor -
                                           s_actor_command_count);
        for (uint16_t i = 0; i < s_actor_draw_count; i++) {
            s_actor_slots[i] = command_slot++;
            if (sm64_mario_texture_tile_start[s_actor_order[i]] !=
                SM64_MARIO_TEXTURE_TILE_NONE)
                s_actor_texture_slots[i] = command_slot++;
        }
        /* Mario stays master-owned.  The old experiment dispatched this
         * actor setup through the slave, which made the textured/Gouraud path
         * depend on a second command producer and left the master fallback as
         * flat RGB polygons.  Lower the already-sorted actor range directly on
         * the master so visual fidelity does not depend on the terrain worker. */
        demo_emit_context_t actor_emit = {
            .primitives = NULL,
            .cmdts = backend->list.cmdts,
            .partitions = partitions,
            .gouraud_tables = NULL,
            .gouraud_addresses = NULL,
            .stats = {{0U, 0U, 0U}, {0U, 0U, 0U}}
        };
        demo_emit_mario_range(&actor_emit, 0U, s_actor_draw_count);
        profile->triangles_vdp1_emitted +=
            actor_emit.stats[0].triangles_emitted;
        profile->triangles_emitted += actor_emit.stats[0].triangles_emitted;
        profile->demo_actor_primitives_emitted += s_actor_draw_count;
        return;
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
    const sm64_saturn_ir_transform_job_t terrain_job = {
        .camera = demo_camera(snapshot),
        .focal_length = DEMO_FOCAL_LENGTH,
        .near_depth = SATURN_DEMO_NEAR_DEPTH,
        .center_x = DEMO_CENTER_X,
        .center_y = DEMO_CENTER_Y,
        .coord_min = DEMO_COORD_MIN,
        .coord_max = DEMO_COORD_MAX,
        .clip_near = SATURN_DEMO_NEAR_CLIP != 0
    };
    const sm64_saturn_ir_transform_job_t actor_job = {
        .camera = terrain_job.camera,
        .focal_length = terrain_job.focal_length,
        .near_depth = terrain_job.near_depth,
        .center_x = terrain_job.center_x,
        .center_y = terrain_job.center_y,
        .coord_min = terrain_job.coord_min,
        .coord_max = terrain_job.coord_max,
        .clip_near = false
    };
    demo_mario_transform_context_t mario_transform = {
        .job = &actor_job, .snapshot = snapshot, .pose = pose
    };
    /* Mario is small and remains a master-owned actor pass. Terrain starts
     * its one coarse producer below; keeping this call here preserves actor
     * determinism without granting the worker any VDP1 or game state. */
    demo_transform_mario_range(&mario_transform, 0U, SM64_MARIO_VERTEX_COUNT);
    const uint16_t actor_command_count =
        demo_prepare_mario(snapshot, pose);
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    demo_spatial_admit(&terrain_job.camera, profile);
#endif
    demo_prepare_render_work_order();
    const uint16_t work_split = demo_choose_work_split();
#if SATURN_SLAVE_RENDER
    const bool dual_transform_phase = work_split < s_render_work_count;
#else
    const bool dual_transform_phase = false;
#endif
    memset(s_position_valid, 0, sizeof(s_position_valid));
    s_transform_phase_ready[0] = 0U;
    s_transform_phase_ready[1] = 0U;
    s_transform_phase_failed = 0U;
    demo_prepare_position_owners(work_split, dual_transform_phase);
    memset(s_primitive_lod_transition, 0,
           sizeof(s_primitive_lod_transition));
    memset(s_primitive_lod_suppressed, 0,
           sizeof(s_primitive_lod_suppressed));
    memset(s_primitive_lod_texture_downgraded, 0,
           sizeof(s_primitive_lod_texture_downgraded));
    demo_classify_context_t classify = {
        .primitives = s_bob_primitives_active,
        .work_order = s_render_work_order,
        .camera = &terrain_job.camera,
        .job = &terrain_job,
        .visible = {0U, 0U},
        .transformed = {0U, 0U},
        .radius_rejected = {0U, 0U},
        .near_rejected = {0U, 0U},
        .degenerate = {0U, 0U},
        .dual_phase = dual_transform_phase
    };
    sm64_saturn_terrain_result_spans_init(
        &s_terrain_spans_shared,
        sm64_saturn_terrain_result_records(s_terrain_master_results),
        DEMO_TERRAIN_RESULT_CAPACITY,
        sm64_saturn_terrain_result_records(s_terrain_slave_results),
        DEMO_TERRAIN_RESULT_CAPACITY, 8U);
    demo_terrain_compact_context_t compact = {
        .classify = &classify, .spans = &s_terrain_spans_shared};
    sm64_saturn_dual_worker_stats_t classify_stats;
    bool classify_ok = true;
#if SATURN_SLAVE_RENDER
    const sm64_saturn_terrain_worker_job_t terrain_worker = {
        .range = demo_terrain_compact_range,
        .context = &compact,
        .count = s_render_work_count,
        .slave_begin = work_split};
    s_slave_begin = terrain_worker.slave_begin;
    classify_ok = sm64_saturn_terrain_worker_run(&terrain_worker,
                                                 &classify_stats) &&
                  s_transform_phase_failed == 0U;
#else
    classify_stats = (sm64_saturn_dual_worker_stats_t){0};
    demo_terrain_compact_range(&compact, 0U, s_render_work_count);
#endif
    if (!classify_ok) {
        memset(s_primitive_visible, 0, sizeof(s_primitive_visible));
        memset(s_position_valid, 0, sizeof(s_position_valid));
        memset(classify.visible, 0, sizeof(classify.visible));
        memset(classify.transformed, 0, sizeof(classify.transformed));
        memset(classify.radius_rejected, 0, sizeof(classify.radius_rejected));
        memset(classify.near_rejected, 0, sizeof(classify.near_rejected));
        memset(classify.degenerate, 0, sizeof(classify.degenerate));
        memset(classify.clip_away, 0, sizeof(classify.clip_away));
        memset(classify.clip_to_one, 0, sizeof(classify.clip_to_one));
        memset(classify.clip_to_two, 0, sizeof(classify.clip_to_two));
        memset(classify.clip_recovery, 0, sizeof(classify.clip_recovery));
        memset(classify.clip_overflow, 0, sizeof(classify.clip_overflow));
        s_transform_phase_ready[0] = 0U;
        s_transform_phase_ready[1] = 0U;
        s_transform_phase_failed = 0U;
        classify.dual_phase = false;
        demo_prepare_position_owners(s_render_work_count, false);
        sm64_saturn_terrain_result_spans_init(
            &s_terrain_spans_shared,
            sm64_saturn_terrain_result_records(s_terrain_master_results),
            DEMO_TERRAIN_RESULT_CAPACITY,
            sm64_saturn_terrain_result_records(s_terrain_slave_results),
            DEMO_TERRAIN_RESULT_CAPACITY, 8U);
        demo_terrain_compact_range(&compact, 0U, s_render_work_count);
    }
    profile->slave_jobs_completed += classify_stats.slave_jobs_completed;
    profile->slave_busy_ticks += classify_stats.slave_busy_ticks;
    profile->master_wait_ticks += classify_stats.master_wait_ticks;
    profile->slave_timeouts += classify_stats.slave_timeouts;
    s_last_master_wait_ticks = classify_stats.master_wait_ticks > UINT16_MAX
        ? UINT16_MAX : (uint16_t)classify_stats.master_wait_ticks;
    profile->triangles_transformed += classify.transformed[0] +
                                     classify.transformed[1];
    profile->demo_bob_primitives_visible += classify.visible[0] +
                                            classify.visible[1];
    profile->demo_bob_primitives_radius_rejected +=
        classify.radius_rejected[0] + classify.radius_rejected[1];
    profile->demo_bob_primitives_near_rejected +=
        classify.near_rejected[0] + classify.near_rejected[1];
    profile->demo_bob_primitives_degenerate +=
        classify.degenerate[0] + classify.degenerate[1];
    profile->demo_bob_clip_away += classify.clip_away[0] + classify.clip_away[1];
    profile->demo_bob_clip_to_one += classify.clip_to_one[0] + classify.clip_to_one[1];
    profile->demo_bob_clip_to_two += classify.clip_to_two[0] + classify.clip_to_two[1];
    profile->demo_bob_clip_recovery += classify.clip_recovery[0] + classify.clip_recovery[1];
    profile->demo_bob_clip_overflow += classify.clip_overflow[0] + classify.clip_overflow[1];
    profile->demo_bob_results_master += s_terrain_spans_shared.master.count;
    profile->demo_bob_results_slave += s_terrain_spans_shared.slave.count;
    profile->demo_bob_result_reserve_rejects +=
        s_terrain_spans_shared.master.reserve_rejects + s_terrain_spans_shared.slave.reserve_rejects;
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++) {
        switch (s_primitive_lod_tier[primitive]) {
        case 2U: profile->demo_lod_tier_far++; break;
        case 1U: profile->demo_lod_tier_mid++; break;
        default: profile->demo_lod_tier_near++; break;
        }
        profile->demo_lod_transitions +=
            s_primitive_lod_transition[primitive];
        profile->demo_lod_primitives_suppressed +=
            s_primitive_lod_suppressed[primitive];
        profile->demo_lod_texture_downgrades +=
            s_primitive_lod_texture_downgraded[primitive];
    }
#if 0 /* retained only as a historical diagnostic; compact results are authoritative */
#if SATURN_DEMO_BSP_FRAGMENTS
    memset(s_bsp_seen, 0, sizeof(s_bsp_seen));
    s_emit_count = 0U;
    demo_fragment_bsp_append(0, &terrain_job.camera);
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
    demo_bsp_append(0, &terrain_job.camera);
    for (uint16_t i = 0U; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++) {
        if (s_primitive_visible[i] != 0U && s_bsp_seen[i] == 0U)
            s_emit_order[s_emit_count++] = i;
    }
#endif
#elif 0 /* legacy bucket painter; no longer part of the render path */
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
    demo_merge_terrain_results(&s_terrain_spans_shared);
    sm64_saturn_gouraud_bank_begin(gouraud_bank);
    /* Essential actor shading is reserved before optional world shading.
     * Previously terrain consumed the Gouraud bank first, which made Mario
     * flat even on frames where his command batch happened to fit. */
    demo_reserve_mario_gouraud(gouraud_bank, profile);
    sm64_saturn_vdp1_backend_begin(backend);
    vdp1_vram_partitions_t partitions;
    vdp1_vram_partitions_get(&partitions);
    demo_resolve_terrain_command_templates(&partitions);
    /* Preserve Mario's all-or-nothing textured tail batch, then retain the
     * nearest terrain results if the command arena is oversubscribed.
     * Z-Treme traverses near-to-far specifically so buffer exhaustion keeps
     * nearby geometry (ZT_RENDERING.c:494-503); our painter stream must still
     * be emitted far-to-near, so select its near tail before lowering it. */
    const uint16_t terrain_command_budget =
        sm64_saturn_command_arena_budget_before_tail(
            &backend->commands, actor_command_count);
    const uint16_t terrain_first =
        s_terrain_emit_count > terrain_command_budget
        ? (uint16_t)(s_terrain_emit_count - terrain_command_budget) : 0U;
    profile->reject_vdp1_arena_capacity += terrain_first;
    /* The master owns all VDP1 lowering. Compact terrain results are merged
     * above, then consumed in the stable baked painter order here. */
    for (uint16_t ordinal = terrain_first;
         ordinal < s_terrain_emit_count; ordinal++) {
        const sm64_saturn_terrain_result_t *result =
            s_terrain_emit_refs[ordinal].record;
        if (result->primitive_id >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;
        demo_emit_terrain_result(
            result, &s_bob_primitives_active[result->primitive_id], backend,
            gouraud_bank, profile, &partitions);
    }
    /* Mario remains master-owned and consumes the live bridge pose, textured
     * material bindings, and per-vertex Gouraud data after terrain compaction.
     * The 68000 stays out of this path; as in Z-Treme and SlaveDriver it is
     * reserved for SCSP/audio service rather than geometry dispatch. */
    demo_emit_mario(snapshot, pose, backend, &partitions, profile);
    if (sm64_saturn_gouraud_bank_used_bytes(gouraud_bank) > 0U) {
        saturn_dma_queue_transfer_wait(
            (void *)gouraud_bank->vram_base, gouraud_bank->staging,
            sm64_saturn_gouraud_bank_used_bytes(gouraud_bank),
            SATURN_DMA_QUEUE_SCU);
    }
    sm64_saturn_vdp1_backend_finish(backend);
#if 0 && SATURN_SLAVE_RENDER && defined(SM64_SATURN_VDP1_LWRAM_STAGING) && \
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
