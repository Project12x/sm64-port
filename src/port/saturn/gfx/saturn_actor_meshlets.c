#include "saturn_actor_meshlets.h"

#include <limits.h>
#include <string.h>

#include "saturn_matrix_kernels.h"
#include "saturn_mario_actor_mesh.h"

#define SM64_SATURN_ACTOR_DEPTH_BIN_COUNT 64U
#define SM64_SATURN_ACTOR_DEPTH_BIN_SHIFT 7U

static uint8_t actor_depth_bin(int32_t depth_q16)
{
    if (depth_q16 <= 0) return 0U;
    const uint32_t depth = (uint32_t)depth_q16 >> 16;
    const uint32_t bin = depth >> SM64_SATURN_ACTOR_DEPTH_BIN_SHIFT;
    return (uint8_t)(bin >= SM64_SATURN_ACTOR_DEPTH_BIN_COUNT
        ? SM64_SATURN_ACTOR_DEPTH_BIN_COUNT - 1U : bin);
}

typedef struct actor_meshlet_depth_bounds {
    int32_t nearest_q16;
    int32_t furthest_q16;
} actor_meshlet_depth_bounds_t;

/* Admission reads the live pose, not neutral bounds. Furthest depth rejects
 * only wholly-behind meshlets and orders translucent bins; nearest depth makes
 * LOD conservative for any visible extent. */
static bool actor_meshlet_live_depth_bounds(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view, uint16_t meshlet,
    actor_meshlet_depth_bounds_t *bounds)
{
    if (bounds == NULL) return false;
    const uint16_t first = sm64_mario_meshlet_lod_position_offsets[meshlet * 3U];
    const uint16_t end = sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + 1U];
    if (end <= first || end > SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT)
        return false;
    const int32_t sine = sm64_saturn_sins_q16(snapshot->mario.yaw);
    const int32_t cosine = sm64_saturn_coss_q16(snapshot->mario.yaw);
    int64_t nearest = INT64_MAX;
    int64_t furthest = INT64_MIN;
    for (uint16_t index = first; index < end; index++) {
        const uint16_t vertex = sm64_mario_meshlet_lod_position_list[index];
        if (vertex >= pose->vertex_count) return false;
        const int64_t x = pose->vertices[vertex][0];
        const int64_t z = pose->vertices[vertex][2];
        const int64_t world_x = (int64_t)snapshot->mario.position[0] +
            ((x * cosine + z * sine) >> 16);
        const int64_t world_y = (int64_t)snapshot->mario.position[1] +
            pose->vertices[vertex][1];
        const int64_t world_z = (int64_t)snapshot->mario.position[2] +
            ((-x * sine + z * cosine) >> 16);
        const int64_t depth =
            ((((world_x << 16) - view->camera_position_q16[0]) *
              view->view_forward_q16[0]) >> 16) +
            ((((world_y << 16) - view->camera_position_q16[1]) *
              view->view_forward_q16[1]) >> 16) +
            ((((world_z << 16) - view->camera_position_q16[2]) *
              view->view_forward_q16[2]) >> 16);
        if (depth < nearest) nearest = depth;
        if (depth > furthest) furthest = depth;
    }
    bounds->nearest_q16 = nearest > INT32_MAX ? INT32_MAX :
        nearest < INT32_MIN ? INT32_MIN : (int32_t)nearest;
    bounds->furthest_q16 = furthest > INT32_MAX ? INT32_MAX :
        furthest < INT32_MIN ? INT32_MIN : (int32_t)furthest;
    return true;
}

static uint8_t actor_lod_tier(int32_t depth_q16)
{
    const uint32_t depth = depth_q16 > 0 ? (uint32_t)depth_q16 >> 16 : 0U;
    if (depth >= 4096U) return 2U;
    if (depth >= 2048U) return 1U;
    return 0U;
}

static bool actor_meshlet_span_valid(uint16_t meshlet, uint8_t tier)
{
#if defined(SM64_SATURN_ACTOR_MESHLET_TEST_INVALID_SPAN)
    if (meshlet == 0U && tier == 0U) return false;
#endif
    const uint16_t primitive_first =
        sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier];
    const uint16_t primitive_end =
        sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier + 1U];
    const uint16_t position_first =
        sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier];
    const uint16_t position_end =
        sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier + 1U];
    return primitive_end >= primitive_first &&
        primitive_end <= SM64_MARIO_MESHLET_LOD_PRIMITIVE_LIST_COUNT &&
        position_end >= position_first &&
        position_end <= SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT;
}

static bool actor_meshlet_input_valid(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    const sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity)
{
    return snapshot != NULL && pose != NULL && view != NULL && output != NULL &&
        output->opaque != NULL && output->translucent != NULL &&
        output->positions != NULL && output->position_capacity != 0U &&
        capacity != 0U &&
        snapshot->generation != 0U && snapshot->actor_generation != 0U &&
        snapshot->mario.valid != 0U && pose->vertices != NULL &&
        pose->vertex_count == SM64_MARIO_VERTEX_COUNT && view->generation != 0U &&
        (view->view_forward_q16[0] != 0 || view->view_forward_q16[1] != 0 ||
         view->view_forward_q16[2] != 0);
}

bool sm64_saturn_actor_meshlets_prepare(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats)
{
    uint16_t opaque_count = 0U;
    uint16_t translucent_count = 0U;
    uint16_t admitted_positions = 0U;
    uint16_t admitted_meshlets = 0U;
    uint16_t culled = 0U;
    uint16_t translucent_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT] = {0};
    uint8_t meshlet_admitted[SM64_MARIO_MESHLET_COUNT] = {0};
    uint8_t meshlet_tier[SM64_MARIO_MESHLET_COUNT] = {0};
    int32_t meshlet_sort_depth[SM64_MARIO_MESHLET_COUNT] = {0};
    uint8_t position_seen[SM64_MARIO_VERTEX_COUNT] = {0};

    if (output == NULL) return false;
    output->opaque_count = 0U;
    output->translucent_count = 0U;
    output->position_count = 0U;
    if (!actor_meshlet_input_valid(snapshot, pose, view, output, capacity))
        return false;

    for (uint16_t meshlet = 0U; meshlet < SM64_MARIO_MESHLET_COUNT; meshlet++) {
        actor_meshlet_depth_bounds_t depth_bounds;
        if (stats != NULL) stats->demo_actor_meshlets_tested++;
        if (!actor_meshlet_live_depth_bounds(snapshot, pose, view, meshlet,
                                             &depth_bounds))
            return false;
        if (depth_bounds.furthest_q16 <= 0) {
            culled++;
            continue;
        }
        const uint8_t tier = actor_lod_tier(depth_bounds.nearest_q16);
        if (!actor_meshlet_span_valid(meshlet, tier)) return false;
        const uint16_t primitive_first =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier];
        const uint16_t primitive_end =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier + 1U];
        const uint16_t position_first =
            sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier];
        const uint16_t position_end =
            sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier + 1U];
        if (primitive_end == primitive_first) continue;
        meshlet_admitted[meshlet] = 1U;
        meshlet_tier[meshlet] = tier;
        meshlet_sort_depth[meshlet] = depth_bounds.furthest_q16;
        admitted_meshlets++;
        for (uint16_t position_index = position_first;
             position_index < position_end; position_index++) {
            const uint16_t position =
                sm64_mario_meshlet_lod_position_list[position_index];
            if (position >= pose->vertex_count) return false;
            if (position_seen[position] != 0U) continue;
            if (admitted_positions >= output->position_capacity) return false;
            position_seen[position] = 1U;
            output->positions[admitted_positions++] = position;
        }
        if (sm64_mario_meshlet_opacity[meshlet] ==
            SM64_MARIO_MESHLET_OPACITY_TRANSLUCENT) {
            const uint8_t bin = actor_depth_bin(depth_bounds.furthest_q16);
            translucent_count = (uint16_t)(translucent_count +
                primitive_end - primitive_first);
            translucent_bins[bin] = (uint16_t)(translucent_bins[bin] +
                primitive_end - primitive_first);
        } else {
            opaque_count = (uint16_t)(opaque_count + primitive_end - primitive_first);
        }
        if ((uint32_t)opaque_count + translucent_count > capacity) return false;
    }
    if ((uint32_t)opaque_count + translucent_count > capacity) return false;

    uint16_t opaque_cursor = 0U;
    uint16_t translucent_cursor[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT];
    uint16_t cursor = 0U;
    for (int16_t bin = SM64_SATURN_ACTOR_DEPTH_BIN_COUNT - 1U; bin >= 0; bin--) {
        translucent_cursor[bin] = cursor;
        cursor = (uint16_t)(cursor + translucent_bins[bin]);
    }
    for (uint16_t meshlet = 0U; meshlet < SM64_MARIO_MESHLET_COUNT; meshlet++) {
        if (meshlet_admitted[meshlet] == 0U) continue;
        const uint8_t tier = meshlet_tier[meshlet];
        const uint16_t first =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier];
        const uint16_t end =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier + 1U];
        const uint8_t bin = actor_depth_bin(meshlet_sort_depth[meshlet]);
        for (uint16_t index = first; index < end; index++) {
            const sm64_saturn_actor_draw_ref_t ref = {
                .meshlet_id = meshlet,
                .primitive_id = sm64_mario_meshlet_lod_primitive_list[index],
                .sort_key = ((uint32_t)bin << 16) |
                    sm64_mario_meshlet_lod_primitive_list[index],
            };
            if (sm64_mario_meshlet_opacity[meshlet] ==
                SM64_MARIO_MESHLET_OPACITY_TRANSLUCENT) {
                output->translucent[translucent_cursor[bin]++] = ref;
            } else {
                output->opaque[opaque_cursor++] = ref;
            }
        }
    }
    output->opaque_count = opaque_count;
    output->translucent_count = translucent_count;
    output->position_count = admitted_positions;
    if (stats != NULL) {
        stats->demo_actor_meshlets_admitted += admitted_meshlets;
        stats->demo_actor_meshlets_culled += culled;
        stats->demo_actor_positions_admitted += admitted_positions;
    }
    return true;
}
