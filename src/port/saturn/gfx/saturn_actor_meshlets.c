#include "saturn_actor_meshlets.h"

#include <limits.h>
#include <string.h>

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

static int32_t actor_meshlet_depth_q16(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_render_view_t *view, uint16_t meshlet)
{
    int64_t dot = 0;
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        const int32_t center = ((int32_t)sm64_mario_meshlet_bounds[meshlet][0][axis] +
                                (int32_t)sm64_mario_meshlet_bounds[meshlet][1][axis]) / 2;
        const int64_t relative =
            ((int64_t)((int64_t)snapshot->mario.position[axis] + center) *
             (1 << 16)) -
            view->camera_position_q16[axis];
        dot += (relative * view->view_forward_q16[axis]) >> 16;
    }
    if (dot > INT32_MAX) return INT32_MAX;
    if (dot < INT32_MIN) return INT32_MIN;
    return (int32_t)dot;
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
        output->opaque != NULL && output->translucent != NULL && capacity != 0U &&
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
    uint16_t culled = 0U;
    uint16_t translucent_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT] = {0};

    if (output == NULL) return false;
    output->opaque_count = 0U;
    output->translucent_count = 0U;
    if (!actor_meshlet_input_valid(snapshot, pose, view, output, capacity))
        return false;

    for (uint16_t meshlet = 0U; meshlet < SM64_MARIO_MESHLET_COUNT; meshlet++) {
        const int32_t depth = actor_meshlet_depth_q16(snapshot, view, meshlet);
        const uint8_t tier = actor_lod_tier(depth);
        if (stats != NULL) stats->demo_actor_meshlets_tested++;
        if (depth <= 0) {
            culled++;
            continue;
        }
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
        admitted_positions = (uint16_t)(admitted_positions +
            (position_end - position_first));
        if (sm64_mario_meshlet_opacity[meshlet] ==
            SM64_MARIO_MESHLET_OPACITY_TRANSLUCENT) {
            const uint8_t bin = actor_depth_bin(depth);
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
        const int32_t depth = actor_meshlet_depth_q16(snapshot, view, meshlet);
        const uint8_t tier = actor_lod_tier(depth);
        if (depth <= 0) continue;
        const uint16_t first =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier];
        const uint16_t end =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier + 1U];
        const uint8_t bin = actor_depth_bin(depth);
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
    if (stats != NULL) {
        stats->demo_actor_meshlets_admitted +=
            (uint32_t)(SM64_MARIO_MESHLET_COUNT - culled);
        stats->demo_actor_meshlets_culled += culled;
        stats->demo_actor_positions_admitted += admitted_positions;
    }
    return true;
}
