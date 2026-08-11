#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static int output_records_are(const sm64_saturn_actor_draw_ref_t *records,
                              const uint16_t *positions,
                              uint16_t draw_capacity,
                              uint16_t position_capacity,
                              uint8_t value)
{
    const uint8_t *record_bytes = (const uint8_t *)(const void *)records;
    const uint8_t *position_bytes = (const uint8_t *)(const void *)positions;
    for (size_t index = 0U;
         index < (size_t)draw_capacity * sizeof(*records); index++) {
        if (record_bytes[index] != value) return 0;
    }
    for (size_t index = 0U;
         index < (size_t)position_capacity * sizeof(*positions); index++) {
        if (position_bytes[index] != value) return 0;
    }
    return 1;
}

static int bank_driven_cases(const char *path)
{
    FILE *file = fopen(path, "rb");
    sm64_saturn_actor_bank_view_t bank;
    sm64_saturn_actor_instance_snapshot_t instance;
    sm64_saturn_render_view_t view = forward_view();
    sm64_saturn_actor_pose_work_t pose_work;
    sm64_saturn_actor_meshlet_bank_output_t binding;
    sm64_saturn_fast3d_profile_t stats = {0};
    sm64_saturn_actor_draw_ref_t *records = NULL;
    uint16_t *positions = NULL;
    int16_t (*posed_vertices)[3] = NULL;
    uint8_t *lights = NULL;
    int32_t *joint_matrices = NULL;
    int32_t *expected_joint_matrices = NULL;
    uint8_t *position_seen = NULL;
    uint8_t *bytes = NULL;
    long file_size;
    int result = 1;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (file_size = ftell(file)) <= 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "cannot open validated actor bank fixture\n");
        if (file != NULL) fclose(file);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)file_size);
    if (bytes == NULL || fread(bytes, 1U, (size_t)file_size, file) !=
                             (size_t)file_size ||
        !sm64_saturn_actor_bank_validate(bytes, (size_t)file_size, &bank)) {
        fprintf(stderr, "cannot validate actor bank fixture\n");
        result = 0;
        goto cleanup;
    }
    records = (sm64_saturn_actor_draw_ref_t *)malloc(
        (size_t)bank.bank.primitive_count * sizeof(*records));
    positions = (uint16_t *)malloc(
        (size_t)bank.bank.vertex_count * sizeof(*positions));
    posed_vertices = (int16_t (*)[3])malloc(
        (size_t)bank.bank.vertex_count * sizeof(*posed_vertices));
    lights = (uint8_t *)malloc((size_t)bank.bank.vertex_count);
    joint_matrices = (int32_t *)malloc(
        (size_t)bank.bank.joint_count * 16U * sizeof(*joint_matrices));
    expected_joint_matrices = (int32_t *)malloc(
        (size_t)bank.bank.joint_count * 16U *
        sizeof(*expected_joint_matrices));
    position_seen = (uint8_t *)malloc((size_t)bank.bank.vertex_count);
    if (records == NULL || positions == NULL ||
        posed_vertices == NULL || lights == NULL || joint_matrices == NULL ||
        expected_joint_matrices == NULL || position_seen == NULL) {
        fprintf(stderr, "cannot allocate actor bank fixture spans\n");
        result = 0;
        goto cleanup;
    }
    memset(&instance, 0, sizeof(instance));
    instance.generation = view.generation;
    instance.scene_package_generation = 9U;
    instance.instance_key = 0x1234U;
    instance.actor_bank_id = 7U;
    memcpy(instance.actor_bank_hash_words, bank.bank.source_hash_words,
           sizeof(instance.actor_bank_hash_words));
    instance.family_id = bank.bank.family_id;
    instance.model_id = bank.bank.model_id;
    instance.position_q16[2] = 512 << 16;
    /* A small but nonzero rigid scale keeps every bank-declared meshlet in
     * the near tier, so the expected primitive count comes from the S64B
     * header rather than Mario-specific constants. */
    instance.scale_q16[0] = 1;
    instance.scale_q16[1] = 1;
    instance.scale_q16[2] = 1;
    instance.animation_id = 197;
    instance.animation_frame = 0;
    instance.active = 1U;
    instance.render_active = 1U;
    pose_work = (sm64_saturn_actor_pose_work_t){
        .vertices = posed_vertices,
        .light_intensity = lights,
        .joint_matrices_q16 = joint_matrices,
        .vertex_capacity = bank.bank.vertex_count,
        .joint_capacity = bank.bank.joint_count,
        .light_capacity = bank.bank.vertex_count,
    };
    memset(&binding, 0, sizeof(binding));
    binding.output = (sm64_saturn_actor_meshlet_output_t){
        .positions = positions,
        .position_capacity = bank.bank.vertex_count,
    };
    binding.records = records;
    binding.position_seen = position_seen;
    binding.abi = SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI;
    binding.draw_capacity = bank.bank.primitive_count;
    binding.position_seen_capacity = bank.bank.vertex_count;
    {
        sm64_saturn_actor_pose_view_t expected_pose;
        if (!sm64_saturn_actor_pose_evaluate(
                &bank, instance.animation_id, instance.animation_frame,
                &pose_work, &expected_pose)) {
            fprintf(stderr, "cannot evaluate bank pose preservation fixture\n");
            result = 0;
            goto cleanup;
        }
        memcpy(expected_joint_matrices, joint_matrices,
               (size_t)bank.bank.joint_count * 16U *
                   sizeof(*expected_joint_matrices));
    }
    if (!sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &pose_work, &binding, &stats) ||
        (uint32_t)binding.output.opaque_count +
            binding.output.translucent_count !=
            bank.bank.primitive_count ||
        binding.output.position_count == 0U ||
        binding.output.position_count > bank.bank.vertex_count ||
        binding.output.opaque != records ||
        binding.output.translucent != records + binding.output.opaque_count ||
        binding.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE) {
        fprintf(stderr, "bank-driven meshlets did not follow the S64B record spans "
                "(opaque=%u translucent=%u positions=%u primitives=%u "
                "reason=%u)\n", binding.output.opaque_count,
                binding.output.translucent_count,
                binding.output.position_count, bank.bank.primitive_count,
                binding.quarantine_reason);
        result = 0;
        goto cleanup;
    }
    for (uint16_t index = 0U; index < binding.output.opaque_count; index++) {
        if (binding.output.opaque[index].meshlet_id >= bank.bank.meshlet_count ||
            binding.output.opaque[index].primitive_id >= bank.bank.primitive_count) {
            fprintf(stderr, "bank-driven opaque output escaped declared spans\n");
            result = 0;
            goto cleanup;
        }
    }
    for (uint16_t index = 0U; index < binding.output.translucent_count; index++) {
        if (binding.output.translucent[index].meshlet_id >=
                bank.bank.meshlet_count ||
            binding.output.translucent[index].primitive_id >=
                bank.bank.primitive_count) {
            fprintf(stderr, "bank-driven translucent output escaped declared spans\n");
            result = 0;
            goto cleanup;
        }
    }
    for (uint16_t vertex = 0U; vertex < bank.bank.vertex_count; vertex++) {
        if (lights[vertex] != SM64_SATURN_ACTOR_POSE_LIGHT_DEFAULT) {
            fprintf(stderr, "meshlet preparation destroyed evaluated lighting\n");
            result = 0;
            goto cleanup;
        }
    }
    if (memcmp(joint_matrices, expected_joint_matrices,
               (size_t)bank.bank.joint_count * 16U *
                   sizeof(*joint_matrices)) != 0) {
        fprintf(stderr, "meshlet preparation destroyed evaluated joints\n");
        result = 0;
        goto cleanup;
    }

    memset(records, 0xA5,
           (size_t)bank.bank.primitive_count * sizeof(*records));
    memset(positions, 0xA5, (size_t)bank.bank.vertex_count * sizeof(*positions));
    binding.draw_capacity = (uint16_t)(bank.bank.primitive_count - 1U);
    if (sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &pose_work, &binding, &stats) ||
        binding.output.opaque_count != 0U ||
        binding.output.translucent_count != 0U ||
        binding.output.position_count != 0U ||
        binding.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW ||
        !output_records_are(records, positions,
                            bank.bank.primitive_count, bank.bank.vertex_count,
                            0xA5U)) {
        fprintf(stderr, "bank output overflow did not quarantine atomically\n");
        result = 0;
        goto cleanup;
    }

    binding.draw_capacity = bank.bank.primitive_count;
    instance.actor_bank_hash_words[7] ^= 1U;
    if (sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &pose_work, &binding, &stats) ||
        binding.output.opaque_count != 0U ||
        binding.output.translucent_count != 0U ||
        binding.output.position_count != 0U ||
        binding.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK ||
        !output_records_are(records, positions,
                            bank.bank.primitive_count, bank.bank.vertex_count,
                            0xA5U)) {
        fprintf(stderr, "stale bank identity was not rejected before output writes\n");
        result = 0;
        goto cleanup;
    }

    memcpy(instance.actor_bank_hash_words, bank.bank.source_hash_words,
           sizeof(instance.actor_bank_hash_words));
    instance.generation = view.generation + 1U;
    if (sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &pose_work, &binding, &stats) ||
        binding.output.opaque_count != 0U ||
        binding.output.translucent_count != 0U ||
        binding.output.position_count != 0U ||
        binding.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_GENERATION ||
        !output_records_are(records, positions,
                            bank.bank.primitive_count, bank.bank.vertex_count,
                            0xA5U)) {
        fprintf(stderr,
                "stale generation was not rejected before output writes\n");
        result = 0;
    }

cleanup:
    if (file != NULL) fclose(file);
    free(position_seen);
    free(expected_joint_matrices);
    free(joint_matrices);
    free(lights);
    free(posed_vertices);
    free(positions);
    free(records);
    free(bytes);
    return result;
}

int main(int argc, char **argv)
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

    if (argc > 2 || (argc == 2 && !bank_driven_cases(argv[1]))) return 1;

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
