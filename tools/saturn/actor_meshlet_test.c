#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "saturn_actor_meshlets.h"
#include "saturn_mario_actor_mesh.h"

static sm64_saturn_render_snapshot_t admitted_snapshot(void)
{
    sm64_saturn_render_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.generation = 1U;
    snapshot.actor_generation = 1U;
    snapshot.mario.valid = 1U;
    snapshot.mario.position[2] = 512;
    return snapshot;
}

static sm64_saturn_mario_actor_pose_t admitted_pose(void)
{
    return (sm64_saturn_mario_actor_pose_t){
        .vertices = sm64_mario_vertices,
        .vertex_count = SM64_MARIO_VERTEX_COUNT,
    };
}

static sm64_saturn_render_view_t forward_view(void)
{
    sm64_saturn_render_view_t view;
    memset(&view, 0, sizeof(view));
    view.generation = 1U;
    view.view_forward_q16[2] = 1 << 16;
    return view;
}

static int has_primitive(const sm64_saturn_actor_draw_ref_t *refs,
                         uint16_t count, uint16_t primitive)
{
    for (uint16_t i = 0U; i < count; i++)
        if (refs[i].primitive_id == primitive) return 1;
    return 0;
}

static int has_meshlet(const sm64_saturn_actor_draw_ref_t *refs,
                       uint16_t count, uint16_t meshlet)
{
    for (uint16_t i = 0U; i < count; i++)
        if (refs[i].meshlet_id == meshlet) return 1;
    return 0;
}

int main(void)
{
    sm64_saturn_actor_draw_ref_t opaque[SM64_MARIO_PRIMITIVE_COUNT];
    sm64_saturn_actor_draw_ref_t translucent[SM64_MARIO_PRIMITIVE_COUNT];
    uint16_t positions[SM64_MARIO_VERTEX_COUNT];
    sm64_saturn_actor_meshlet_output_t output = {
        .opaque = opaque,
        .translucent = translucent,
        .positions = positions,
        .position_capacity = SM64_MARIO_VERTEX_COUNT,
    };
    sm64_saturn_render_snapshot_t snapshot = admitted_snapshot();
    sm64_saturn_mario_actor_pose_t pose = admitted_pose();
    sm64_saturn_render_view_t view = forward_view();
    sm64_saturn_fast3d_profile_t stats = {0};

    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || output.opaque_count + output.translucent_count !=
            SM64_MARIO_PRIMITIVE_COUNT) {
        fprintf(stderr, "fully admitted Mario meshlet output is incomplete\n");
        return 1;
    }
    for (uint16_t i = 1U; i < output.opaque_count; i++) {
        if (output.opaque[i - 1U].meshlet_id > output.opaque[i].meshlet_id ||
            (output.opaque[i - 1U].meshlet_id == output.opaque[i].meshlet_id &&
             output.opaque[i - 1U].primitive_id >= output.opaque[i].primitive_id)) {
            fprintf(stderr, "opaque meshlets do not preserve source order\n");
            return 1;
        }
    }
    for (uint16_t i = 1U; i < output.translucent_count; i++) {
        const uint32_t previous_bin = output.translucent[i - 1U].sort_key >> 16;
        const uint32_t current_bin = output.translucent[i].sort_key >> 16;
        if (previous_bin < current_bin ||
            (previous_bin == current_bin &&
             output.translucent[i - 1U].primitive_id >=
                 output.translucent[i].primitive_id)) {
            fprintf(stderr, "translucent meshlet refs are not stable far-to-near bins\n");
            return 1;
        }
    }
    if (output.translucent_count == 0U) {
        fprintf(stderr, "generated Mario meshlets lost translucent material class\n");
        return 1;
    }
    if (!has_primitive(opaque, output.opaque_count, 0U) &&
        !has_primitive(translucent, output.translucent_count, 0U)) {
        fprintf(stderr, "serial source primitive identity was not retained\n");
        return 1;
    }
    if (output.position_count == 0U ||
        output.position_count != stats.demo_actor_positions_admitted) {
        fprintf(stderr, "meshlet position telemetry does not match transform refs\n");
        return 1;
    }
    for (uint16_t i = 0U; i < output.position_count; i++) {
        if (positions[i] >= SM64_MARIO_VERTEX_COUNT) {
            fprintf(stderr, "meshlet position stream contains an invalid vertex\n");
            return 1;
        }
        for (uint16_t prior = 0U; prior < i; prior++) {
            if (positions[prior] == positions[i]) {
                fprintf(stderr, "meshlet position stream did not globally deduplicate\n");
                return 1;
            }
        }
    }

    snapshot = admitted_snapshot();
    snapshot.mario.position[2] = -30;
    snapshot.mario.yaw = 0;
    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || has_meshlet(opaque, output.opaque_count, 17U) ||
        has_meshlet(translucent, output.translucent_count, 17U)) {
        fprintf(stderr, "neutral view-plane meshlet fixture is not behind camera\n");
        return 1;
    }
    snapshot.mario.yaw = -0x4000;
    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || (!has_meshlet(opaque, output.opaque_count, 17U) &&
        !has_meshlet(translucent, output.translucent_count, 17U))) {
        fprintf(stderr, "yaw-rotated meshlet crossing the view plane was culled\n");
        return 1;
    }

    int16_t animated_vertices[SM64_MARIO_VERTEX_COUNT][3];
    memcpy(animated_vertices, sm64_mario_walking_animation_vertices[0],
           sizeof(animated_vertices));
    const uint16_t animated_first = sm64_mario_meshlet_lod_position_offsets[0U];
    const uint16_t animated_end = sm64_mario_meshlet_lod_position_offsets[1U];
    for (uint16_t i = animated_first; i < animated_end; i++)
        animated_vertices[sm64_mario_meshlet_lod_position_list[i]][2] = 1024;
    pose.vertices = animated_vertices;
    pose.walking_bank = 1U;
    snapshot = admitted_snapshot();
    snapshot.mario.position[2] = -512;
    snapshot.mario.yaw = 0;
    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || (!has_meshlet(opaque, output.opaque_count, 0U) &&
        !has_meshlet(translucent, output.translucent_count, 0U))) {
        fprintf(stderr, "animated walking-pose meshlet was culled from neutral bounds\n");
        return 1;
    }
    pose = admitted_pose();

    memset(&stats, 0, sizeof(stats));
    snapshot.mario.position[2] = -4096;
    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || output.opaque_count != 0U || output.translucent_count != 0U ||
        stats.demo_actor_meshlets_culled == 0U ||
        stats.demo_actor_positions_admitted != 0U) {
        fprintf(stderr, "back-facing meshlets were not rejected before transforms\n");
        return 1;
    }

    snapshot = admitted_snapshot();
    if (sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, 1U, &stats) ||
        output.opaque_count != 0U || output.translucent_count != 0U) {
        fprintf(stderr, "meshlet command-capacity failure did not fail closed\n");
        return 1;
    }

    pose.vertex_count--;
    if (sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats)) {
        fprintf(stderr, "invalid generated meshlet pose span was accepted\n");
        return 1;
    }

    puts("actor meshlet fixture: PASS");
    return 0;
}
