#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_actor_batch.h"
#include "saturn_actor_meshlets.h"
#include "saturn_mario_actor_mesh.h"

static sm64_saturn_actor_runtime_storage_t runtime_storage;

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

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

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t index = 0U; index < size; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t legacy_output_hash(int32_t z, int16_t yaw, uint16_t capacity)
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
    sm64_saturn_fast3d_profile_t stats;
    uint8_t accepted;
    uint64_t hash = UINT64_C(1469598103934665603);
    memset(opaque, 0xA5, sizeof(opaque));
    memset(translucent, 0xA5, sizeof(translucent));
    memset(positions, 0xA5, sizeof(positions));
    memset(&stats, 0, sizeof(stats));
    snapshot.mario.position[2] = z;
    snapshot.mario.yaw = yaw;
    accepted = sm64_saturn_actor_meshlets_prepare(
        &snapshot, &pose, &view, &output, capacity, &stats);
    hash = hash_bytes(hash, &accepted, sizeof(accepted));
    hash = hash_bytes(hash, &output.opaque_count, sizeof(output.opaque_count));
    hash = hash_bytes(hash, &output.translucent_count,
                      sizeof(output.translucent_count));
    hash = hash_bytes(hash, &output.position_count, sizeof(output.position_count));
    hash = hash_bytes(hash, opaque, sizeof(opaque));
    hash = hash_bytes(hash, translucent, sizeof(translucent));
    hash = hash_bytes(hash, positions, sizeof(positions));
    return hash_bytes(hash, &stats, sizeof(stats));
}

static int legacy_output_hashes_match(void)
{
    return legacy_output_hash(512, 0, SM64_MARIO_PRIMITIVE_COUNT) ==
               UINT64_C(0x0b829cbd1c980a96) &&
        legacy_output_hash(-30, -0x4000, SM64_MARIO_PRIMITIVE_COUNT) ==
               UINT64_C(0xd3dbd9102b6f7cfa) &&
        legacy_output_hash(-4096, 0, SM64_MARIO_PRIMITIVE_COUNT) ==
               UINT64_C(0x509222a1063ae826) &&
        legacy_output_hash(512, 0, 1U) ==
               UINT64_C(0x65d76ae8ccac5820);
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

static int opaque_refs_are_far_to_near(
    const sm64_saturn_actor_meshlet_output_t *output)
{
    for (uint16_t i = 1U; i < output->opaque_count; i++) {
        if ((output->opaque[i - 1U].sort_key >> 16) <
            (output->opaque[i].sort_key >> 16))
            return 0;
    }
    return 1;
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

static int output_matches_declared_tier_zero(
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_meshlet_output_t *output)
{
    const uint8_t *geometry = bank->bytes + bank->meshlets_offset;
    const uint32_t meshlet_offset = read_be32(geometry + 30U);
    const uint32_t primitive_ref_offset = read_be32(geometry + 38U);
    uint32_t expected_count = 0U;
    if (output->translucent_count != 0U || output->opaque == NULL)
        return 0;
    for (uint16_t meshlet = 0U; meshlet < bank->bank.meshlet_count; meshlet++) {
        const uint8_t *record = geometry + meshlet_offset +
            (uint32_t)meshlet * 66U;
        const uint32_t first = read_be32(record + 18U);
        const uint32_t count = read_be32(record + 22U);
        for (uint32_t local = 0U; local < count; local++) {
            const uint16_t primitive = read_be16(
                geometry + primitive_ref_offset + (first + local) * 2U);
            uint16_t matches = 0U;
            for (uint16_t cursor = 0U; cursor < output->opaque_count; cursor++) {
                if (output->opaque[cursor].meshlet_id == meshlet &&
                    output->opaque[cursor].primitive_id == primitive)
                    matches++;
            }
            if (matches != 1U)
                return 0;
            expected_count++;
        }
    }
    return expected_count == output->opaque_count;
}

static int bank_driven_cases(const char *path)
{
    FILE *file = fopen(path, "rb");
    sm64_saturn_actor_bank_view_t source_bank;
    sm64_saturn_actor_bank_view_t bank;
    sm64_saturn_actor_instance_snapshot_t instance;
    sm64_saturn_render_view_t view = forward_view();
    sm64_saturn_actor_meshlet_workspace_t workspace[2];
    sm64_saturn_actor_meshlet_workspace_t rejected;
    sm64_saturn_fast3d_profile_t stats = {0};
    sm64_saturn_actor_draw_ref_t *records = runtime_storage.outputs;
    sm64_saturn_actor_output_record_t *overlap_records = NULL;
    int32_t *expected_joint_matrices = NULL;
    uint8_t *scratch_storage = NULL;
    uint8_t *scratch = NULL;
    uint8_t *family_bytes = NULL;
    uint8_t *bytes = NULL;
    uint32_t lane_bytes = 0U, usable_bytes = 0U, reserved_bytes = 0U;
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
        !sm64_saturn_actor_bank_validate(bytes, (size_t)file_size,
                                         &source_bank)) {
        fprintf(stderr, "cannot validate actor bank fixture\n");
        result = 0;
        goto cleanup;
    }

    family_bytes = (uint8_t *)malloc((size_t)file_size);
    if (family_bytes == NULL) {
        result = 0;
        goto cleanup;
    }
    memcpy(family_bytes, bytes, (size_t)file_size);
    write_be16(family_bytes + 6U, 0x1234U);
    write_be16(family_bytes + 8U, 0x2345U);
    family_bytes[26U] ^= 0x5AU;
    {
        const uint32_t geometry = source_bank.meshlets_offset;
        const uint32_t parts = read_be32(family_bytes + geometry + 22U);
        const uint32_t meshlets = read_be32(family_bytes + geometry + 30U);
        const uint16_t rigid_joint = read_be16(
            family_bytes + geometry + parts);
        for (uint16_t vertex = 0U;
             vertex < source_bank.bank.vertex_count; vertex++) {
            write_be16(family_bytes + source_bank.vertices_offset +
                           (uint32_t)vertex * 10U + 6U,
                       rigid_joint);
            write_be16(family_bytes + source_bank.vertices_offset +
                           (uint32_t)vertex * 10U + 8U,
                       0U);
        }
        for (uint16_t meshlet = 0U;
             meshlet < source_bank.bank.meshlet_count; meshlet++)
            family_bytes[geometry + meshlets + (uint32_t)meshlet * 66U + 4U] =
                0U;
    }
    if (!sm64_saturn_actor_bank_validate(
            family_bytes, (size_t)file_size, &bank) ||
        bank.bank.family_id != 0x1234U || bank.bank.model_id != 0x2345U) {
        fprintf(stderr, "derived non-Mario rigid/opaque bank did not validate\n");
        result = 0;
        goto cleanup;
    }
    if ((uint32_t)bank.bank.primitive_count * 2U >
            SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING ||
        !sm64_saturn_actor_meshlets_workspace_query(
            &bank, &lane_bytes, &usable_bytes, &reserved_bytes) ||
        lane_bytes != 5520U || usable_bytes != 11040U ||
        reserved_bytes != 11043U || reserved_bytes != bank.max_scratch) {
        fprintf(stderr, "actor workspace query disagrees with S64B scratch\n");
        result = 0;
        goto cleanup;
    }
    scratch_storage = (uint8_t *)malloc((size_t)bank.max_scratch + 16U);
    expected_joint_matrices = (int32_t *)malloc(
        (size_t)bank.bank.joint_count * 16U *
        sizeof(*expected_joint_matrices));
    if (scratch_storage == NULL || expected_joint_matrices == NULL) {
        fprintf(stderr, "cannot allocate actor bank fixture spans\n");
        result = 0;
        goto cleanup;
    }
    for (uint8_t residue = 0U; residue < 4U; residue++) {
        const uint32_t leading_bytes = (4U - residue) & 3U;
        uint32_t payload_byte_count = 13U;
        while ((((uintptr_t)scratch_storage + payload_byte_count) & 3U) !=
               residue)
            payload_byte_count++;
        scratch = scratch_storage + payload_byte_count;
        if (!sm64_saturn_actor_meshlets_bind_workspace(
                &bank, scratch, bank.max_scratch, 0U, records,
                bank.bank.primitive_count, &workspace[0]) ||
            !sm64_saturn_actor_meshlets_bind_workspace(
                &bank, scratch, bank.max_scratch, 1U,
                records + bank.bank.primitive_count,
                bank.bank.primitive_count, &workspace[1]) ||
            workspace[0].output.records != runtime_storage.outputs ||
            workspace[0].scratch_offset != leading_bytes ||
            workspace[1].scratch_offset != leading_bytes + lane_bytes ||
            ((uintptr_t)(scratch + workspace[0].scratch_offset) & 3U) != 0U ||
            ((uintptr_t)(scratch + workspace[1].scratch_offset) & 3U) != 0U ||
            workspace[0].scratch_offset + workspace[0].scratch_size >
                workspace[1].scratch_offset ||
            workspace[1].scratch_offset + workspace[1].scratch_size >
                bank.max_scratch ||
            (uint8_t *)(void *)workspace[0].pose_work.vertices +
                    (uint32_t)bank.bank.vertex_count * 3U * sizeof(int16_t) >
                workspace[0].pose_work.light_intensity ||
            workspace[0].pose_work.light_intensity + bank.bank.vertex_count >
                (uint8_t *)(void *)workspace[0].pose_work.joint_matrices_q16 ||
            (uint8_t *)(void *)workspace[0].pose_work.joint_matrices_q16 +
                    (uint32_t)bank.bank.joint_count * 16U * sizeof(int32_t) >
                (uint8_t *)(void *)workspace[0].output.output.positions ||
            (uint8_t *)(void *)workspace[0].output.output.positions +
                    (uint32_t)bank.bank.vertex_count * sizeof(uint16_t) >
                workspace[0].output.position_seen ||
            workspace[0].output.position_seen + bank.bank.vertex_count >
                scratch + workspace[0].scratch_offset +
                    workspace[0].scratch_size ||
            ((uintptr_t)workspace[0].pose_work.joint_matrices_q16 & 3U) != 0U ||
            ((uintptr_t)workspace[1].pose_work.joint_matrices_q16 & 3U) != 0U ||
            ((uintptr_t)workspace[0].output.output.positions & 3U) != 0U ||
            ((uintptr_t)workspace[1].output.output.positions & 3U) != 0U) {
            fprintf(stderr,
                    "actor workspace base-residue %u alignment gate failed\n",
                    residue);
            result = 0;
            goto cleanup;
        }
    }
    {
        uint32_t payload_byte_count = 13U;
        while ((((uintptr_t)scratch_storage + payload_byte_count) & 3U) != 1U)
            payload_byte_count++;
        scratch = scratch_storage + payload_byte_count;
    }
    overlap_records = (sm64_saturn_actor_output_record_t *)(void *)(scratch + 3U);
    if (((uintptr_t)overlap_records &
         (_Alignof(sm64_saturn_actor_output_record_t) - 1U)) != 0U) {
        fprintf(stderr, "overlap fixture formed a misaligned record pointer\n");
        result = 0;
        goto cleanup;
    }
    if (sm64_saturn_actor_meshlets_bind_workspace(
            &bank, scratch, bank.max_scratch - 1U, 0U, records,
            bank.bank.primitive_count, &rejected) ||
        sm64_saturn_actor_meshlets_bind_workspace(
            &bank, scratch, bank.max_scratch,
            SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT, records,
            bank.bank.primitive_count, &rejected) ||
        sm64_saturn_actor_meshlets_bind_workspace(
            &bank, scratch, bank.max_scratch, 0U,
            overlap_records,
            bank.bank.primitive_count, &rejected)) {
        fprintf(stderr, "actor workspace lane ownership/capacity gate failed\n");
        result = 0;
        goto cleanup;
    }
    memset(&instance, 0, sizeof(instance));
    instance.generation = view.generation;
    instance.scene_package_generation = 9U;
    instance.instance_key = 0x1234U;
    /* Numeric bank IDs are resolved to validated views by Task 2; S64B itself
     * binds only family/model/source hash. */
    instance.actor_bank_id = 0U;
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
    {
        sm64_saturn_actor_pose_view_t expected_pose;
        if (!sm64_saturn_actor_pose_evaluate(
                &bank, instance.animation_id, instance.animation_frame,
                &workspace[0].pose_work, &expected_pose)) {
            fprintf(stderr, "cannot evaluate bank pose preservation fixture\n");
            result = 0;
            goto cleanup;
        }
        memcpy(expected_joint_matrices,
               workspace[0].pose_work.joint_matrices_q16,
               (size_t)bank.bank.joint_count * 16U *
                   sizeof(*expected_joint_matrices));
    }
    if (!sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &workspace[0].pose_work,
            &workspace[0].output, &stats) ||
        !output_matches_declared_tier_zero(
            &bank, &workspace[0].output.output) ||
        workspace[0].output.output.opaque_count != bank.bank.primitive_count ||
        workspace[0].output.output.position_count == 0U ||
        workspace[0].output.output.position_count > bank.bank.vertex_count ||
        workspace[0].output.output.opaque != records ||
        workspace[0].output.output.translucent !=
            records + workspace[0].output.output.opaque_count ||
        workspace[0].output.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE) {
        fprintf(stderr, "non-Mario meshlets did not match the declared tier "
                "(opaque=%u translucent=%u positions=%u primitives=%u "
                "reason=%u)\n", workspace[0].output.output.opaque_count,
                workspace[0].output.output.translucent_count,
                workspace[0].output.output.position_count,
                bank.bank.primitive_count,
                workspace[0].output.quarantine_reason);
        result = 0;
        goto cleanup;
    }
    {
        sm64_saturn_render_view_t side_view = view;
        side_view.view_forward_q16[0] = 1 << 16;
        side_view.view_forward_q16[2] = 0;
        instance.scale_q16[0] = 1 << 16;
        instance.scale_q16[1] = 1 << 16;
        instance.scale_q16[2] = 1 << 16;
        if (!sm64_saturn_actor_meshlets_prepare_bank(
                &bank, &instance, &side_view, &workspace[0].pose_work,
                &workspace[0].output, &stats) ||
            workspace[0].output.output.opaque_count < 2U ||
            !opaque_refs_are_far_to_near(&workspace[0].output.output)) {
            fprintf(stderr,
                    "bank opaque meshlets are not far-to-near under side view\n");
            result = 0;
            goto cleanup;
        }
        instance.scale_q16[0] = 1;
        instance.scale_q16[1] = 1;
        instance.scale_q16[2] = 1;
    }
    {
        sm64_saturn_actor_output_record_t saved = records[1];
        uint16_t saved_count = workspace[0].output.output.opaque_count;
        records[1] = records[0];
        if (output_matches_declared_tier_zero(
                &bank, &workspace[0].output.output)) {
            fprintf(stderr, "exact bank oracle accepted a duplicate record\n");
            result = 0;
            goto cleanup;
        }
        records[1] = saved;
        workspace[0].output.output.opaque_count--;
        if (output_matches_declared_tier_zero(
                &bank, &workspace[0].output.output)) {
            fprintf(stderr, "exact bank oracle accepted an omitted record\n");
            result = 0;
            goto cleanup;
        }
        workspace[0].output.output.opaque_count = saved_count;
        records[1].meshlet_id = (uint16_t)(records[1].meshlet_id + 1U);
        if (output_matches_declared_tier_zero(
                &bank, &workspace[0].output.output)) {
            fprintf(stderr, "exact bank oracle accepted a wrong association\n");
            result = 0;
            goto cleanup;
        }
        records[1] = saved;
    }
    for (uint16_t vertex = 0U; vertex < bank.bank.vertex_count; vertex++) {
        if (workspace[0].pose_work.light_intensity[vertex] !=
                SM64_SATURN_ACTOR_POSE_LIGHT_DEFAULT) {
            fprintf(stderr, "meshlet preparation destroyed evaluated lighting\n");
            result = 0;
            goto cleanup;
        }
    }
    if (memcmp(workspace[0].pose_work.joint_matrices_q16,
               expected_joint_matrices,
               (size_t)bank.bank.joint_count * 16U *
                   sizeof(*expected_joint_matrices)) != 0) {
        fprintf(stderr, "meshlet preparation destroyed evaluated joints\n");
        result = 0;
        goto cleanup;
    }

    {
        uint8_t saved_frame_count[2] = {
            family_bytes[bank.records_offset + 8U],
            family_bytes[bank.records_offset + 9U],
        };
        family_bytes[bank.records_offset + 8U] = 0U;
        family_bytes[bank.records_offset + 9U] = 0U;
        if (!sm64_saturn_actor_meshlets_prepare_bank(
                &bank, &instance, &view, &workspace[0].pose_work,
                &workspace[0].output, &stats) ||
            !output_matches_declared_tier_zero(
                &bank, &workspace[0].output.output)) {
            fprintf(stderr, "prepare traversed an unrelated animation record\n");
            result = 0;
            goto cleanup;
        }
        family_bytes[bank.records_offset + 8U] = saved_frame_count[0];
        family_bytes[bank.records_offset + 9U] = saved_frame_count[1];
    }

    memset(records, 0xA5,
           (size_t)bank.bank.primitive_count * sizeof(*records));
    memset(workspace[0].output.output.positions, 0xA5,
           (size_t)bank.bank.vertex_count * sizeof(uint16_t));
    workspace[0].output.draw_capacity =
        (uint16_t)(bank.bank.primitive_count - 1U);
    if (sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &workspace[0].pose_work,
            &workspace[0].output, &stats) ||
        workspace[0].output.output.opaque_count != 0U ||
        workspace[0].output.output.translucent_count != 0U ||
        workspace[0].output.output.position_count != 0U ||
        workspace[0].output.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW ||
        !output_records_are(records, workspace[0].output.output.positions,
                            bank.bank.primitive_count, bank.bank.vertex_count,
                            0xA5U)) {
        fprintf(stderr, "bank output overflow did not quarantine atomically\n");
        result = 0;
        goto cleanup;
    }

    workspace[0].output.draw_capacity = bank.bank.primitive_count;
    instance.actor_bank_hash_words[7] ^= 1U;
    if (sm64_saturn_actor_meshlets_prepare_bank(
            &bank, &instance, &view, &workspace[0].pose_work,
            &workspace[0].output, &stats) ||
        workspace[0].output.output.opaque_count != 0U ||
        workspace[0].output.output.translucent_count != 0U ||
        workspace[0].output.output.position_count != 0U ||
        workspace[0].output.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK ||
        !output_records_are(records, workspace[0].output.output.positions,
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
            &bank, &instance, &view, &workspace[0].pose_work,
            &workspace[0].output, &stats) ||
        workspace[0].output.output.opaque_count != 0U ||
        workspace[0].output.output.translucent_count != 0U ||
        workspace[0].output.output.position_count != 0U ||
        workspace[0].output.quarantine_reason !=
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_GENERATION ||
        !output_records_are(records, workspace[0].output.output.positions,
                            bank.bank.primitive_count, bank.bank.vertex_count,
                            0xA5U)) {
        fprintf(stderr,
                "stale generation was not rejected before output writes\n");
        result = 0;
    }

cleanup:
    if (file != NULL) fclose(file);
    free(expected_joint_matrices);
    free(scratch_storage);
    free(family_bytes);
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

    if (!legacy_output_hashes_match()) {
        fprintf(stderr, "legacy Mario output/object bytes changed\n");
        return 1;
    }
    if (argc > 2 || (argc == 2 && !bank_driven_cases(argv[1]))) return 1;

    if (!sm64_saturn_actor_meshlets_prepare(
            &snapshot, &pose, &view, &output, SM64_MARIO_PRIMITIVE_COUNT,
            &stats) || output.opaque_count + output.translucent_count !=
            SM64_MARIO_PRIMITIVE_COUNT) {
        fprintf(stderr, "fully admitted Mario meshlet output is incomplete\n");
        return 1;
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
