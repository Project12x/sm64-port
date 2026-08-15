#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_actor_batch.h"
#include "saturn_actor_meshlets.h"
#include "saturn_mario_actor_mesh.h"
#include "saturn_matrix_kernels.h"

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

/* ---------------------------------------------------------------------------
 * T2.6 -- depth-arithmetic equivalence oracle.
 *
 * sprint2-t2_5-prepare-mario-audit.md measured actor_meshlet_live_depth_bounds()
 * at 20.7% of the whole frame and traced it to actor_saturating_mul_i64(),
 * which checks overflow by DIVIDING.  Replacing that arithmetic can change
 * numbers, and the numbers feed actor_lod_tier() and actor_depth_bin() -- i.e.
 * what gets drawn.  So the contract is pinned here BEFORE the swap, exactly as
 * T2.3 did for the painter chain.
 *
 * Three independent statements of the same quantity are cross-checked:
 *   1. `reference` -- the pre-T2.6 per-vertex arithmetic, lifted verbatim into
 *      actor_depth_reference() and reached through the test-only probe.
 *   2. `candidate` -- whatever saturn_actor_meshlets.c actually ships today.
 *   3. `model`     -- written here from the algebraic identity, not from either
 *      implementation.  The reference computes
 *          depth(v) = SUM_a floor( (world_a - cam_a) * F_a / 2^16 )
 *      with world_a = P_a + (q_a << 16) and q_a the integer-quantised rotated,
 *      scaled vertex component.  Because q_a * F_a is an exact integer it pulls
 *      straight out of the floor:
 *          depth(v) = SUM_a floor( (P_a - C_a) * F_a / 2^16 ) + SUM_a q_a * F_a
 *                     \______ per-actor constant ______/       \_ per-vertex _/
 *      That pull-out is the entire basis of the per-actor hoist, so the model
 *      evaluates BOTH sides and requires them to agree.  It constrains the
 *      identity rather than restating one side of it.
 *
 * The sweep deliberately spends most of its cases OUTSIDE the domain where the
 * hoist is legal -- INT64/INT32 extremes on both signs, zero and negative
 * scales, saturating positions -- because that is what the divide-based
 * overflow check existed to handle, and the shipped path must still agree with
 * the reference there (by falling back to it).
 * ------------------------------------------------------------------------- */

/* Provided by src/port/saturn/gfx/saturn_actor_meshlets.c under
 * -DSM64_SATURN_ACTOR_MESHLET_DEPTH_REFERENCE, which only this test defines. */
void sm64_saturn_actor_meshlet_depth_probe(
    const int16_t vertex[3], const int32_t scale_q16[3], int16_t yaw,
    const int64_t position_q16[3], const int32_t camera_position_q16[3],
    const int32_t forward_q16[3], int64_t *reference_depth,
    int64_t *candidate_depth, uint8_t *fast_taken);

/* Must mirror saturn_actor_meshlets.c's ACTOR_DEPTH_FAST_* preconditions.  The
 * probe reports which path the shipped code actually took, and the sweep
 * asserts the shipped code is never more permissive than this mirror, so drift
 * between them is a test failure rather than a silent widening. */
#define DEPTH_FAST_UNIT_SCALE_Q16 INT32_C(65536)
#define DEPTH_FAST_POSITION_LIMIT INT64_C(0x10000000000) /* 2^40 */
#define DEPTH_FAST_FORWARD_LIMIT  INT32_C(0x100000)      /* 2^20 */

static int depth_in_fast_domain(const int32_t scale_q16[3],
                                const int64_t position_q16[3],
                                const int32_t forward_q16[3], int16_t yaw)
{
    const int32_t sine = sm64_saturn_sins_q16(yaw);
    const int32_t cosine = sm64_saturn_coss_q16(yaw);
    if (sine > 65536 || sine < -65536 || cosine > 65536 || cosine < -65536)
        return 0;
    for (int axis = 0; axis < 3; axis++) {
        if (scale_q16[axis] != DEPTH_FAST_UNIT_SCALE_Q16) return 0;
        if (position_q16[axis] > DEPTH_FAST_POSITION_LIMIT ||
            position_q16[axis] < -DEPTH_FAST_POSITION_LIMIT) return 0;
        if (forward_q16[axis] > DEPTH_FAST_FORWARD_LIMIT ||
            forward_q16[axis] < -DEPTH_FAST_FORWARD_LIMIT) return 0;
    }
    return 1;
}

/* Only ever called on fast-domain cases, where every intermediate below is
 * proved to stay inside int64 -- so this model is free of the saturating
 * helpers AND free of undefined overflow. */
static void depth_model(const int16_t vertex[3], const int32_t scale_q16[3],
                        int16_t yaw, const int64_t position_q16[3],
                        const int32_t camera_position_q16[3],
                        const int32_t forward_q16[3], int64_t *direct,
                        int64_t *hoisted)
{
    const int64_t sine = sm64_saturn_sins_q16(yaw);
    const int64_t cosine = sm64_saturn_coss_q16(yaw);
    const int64_t scaled_x = (int64_t)vertex[0] * scale_q16[0];
    const int64_t scaled_y = (int64_t)vertex[1] * scale_q16[1];
    const int64_t scaled_z = (int64_t)vertex[2] * scale_q16[2];
    const int64_t quantised[3] = {
        (scaled_x * cosine + scaled_z * sine) >> 32,
        scaled_y >> 16,
        (scaled_z * cosine - scaled_x * sine) >> 32,
    };
    *direct = 0;
    *hoisted = 0;
    for (int axis = 0; axis < 3; axis++) {
        const int64_t forward = forward_q16[axis];
        const int64_t base =
            position_q16[axis] - (int64_t)camera_position_q16[axis];
        *direct += ((base + (quantised[axis] << 16)) * forward) >> 16;
        *hoisted += ((base * forward) >> 16) + quantised[axis] * forward;
    }
}

/* actor_clamp_i64_i32 / actor_lod_tier / actor_depth_bin are file-private to
 * saturn_actor_meshlets.c.  Restated here so a divergence can be reported in
 * the units that are actually visible on screen -- an LOD tier or a painter
 * bin -- rather than only as a raw Q16 delta. */
static int32_t depth_clamp_i32(int64_t value)
{
    return value > INT32_MAX ? INT32_MAX :
        value < INT32_MIN ? INT32_MIN : (int32_t)value;
}

static uint8_t depth_tier_of(int32_t depth_q16)
{
    const uint32_t depth = depth_q16 > 0 ? (uint32_t)depth_q16 >> 16 : 0U;
    if (depth >= 4096U) return 2U;
    if (depth >= 2048U) return 1U;
    return 0U;
}

static uint8_t depth_bin_of(int32_t depth_q16)
{
    uint32_t bin;
    if (depth_q16 <= 0) return 0U;
    bin = ((uint32_t)depth_q16 >> 16) >> 7U;
    return (uint8_t)(bin >= 64U ? 63U : bin);
}

static uint64_t depth_rng(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

typedef struct depth_sweep_tally {
    uint64_t cases;
    uint64_t fast_cases;
    uint64_t fallback_cases;
    uint64_t model_cases;
    uint64_t value_mismatches;
    uint64_t tier_mismatches;
    uint64_t bin_mismatches;
    uint64_t model_mismatches;
    uint64_t domain_mismatches;
    int64_t worst_delta;
} depth_sweep_tally_t;

static void depth_check_case(const int16_t vertex[3],
                             const int32_t scale_q16[3], int16_t yaw,
                             const int64_t position_q16[3],
                             const int32_t camera_position_q16[3],
                             const int32_t forward_q16[3],
                             depth_sweep_tally_t *tally)
{
    int64_t reference = 0, candidate = 0;
    uint8_t fast_taken = 0U;
    const int expected_fast = depth_in_fast_domain(scale_q16, position_q16,
                                                   forward_q16, yaw);
    sm64_saturn_actor_meshlet_depth_probe(
        vertex, scale_q16, yaw, position_q16, camera_position_q16, forward_q16,
        &reference, &candidate, &fast_taken);
    tally->cases++;
    if (fast_taken != 0U) tally->fast_cases++; else tally->fallback_cases++;
    /* The mirror above restates the kernel's declared preconditions, so the
     * two must agree in BOTH directions: a shipped path that is more
     * permissive would be unsound, and one that is more conservative means the
     * documented domain is wrong. */
    if ((fast_taken != 0U) != (expected_fast != 0)) tally->domain_mismatches++;
    if (reference != candidate) {
        const int64_t delta = reference > candidate
            ? reference - candidate : candidate - reference;
        int32_t reference_q16, candidate_q16;
        tally->value_mismatches++;
        if (delta > tally->worst_delta) tally->worst_delta = delta;
        reference_q16 = depth_clamp_i32(reference);
        candidate_q16 = depth_clamp_i32(candidate);
        if (depth_tier_of(reference_q16) != depth_tier_of(candidate_q16))
            tally->tier_mismatches++;
        if (depth_bin_of(reference_q16) != depth_bin_of(candidate_q16))
            tally->bin_mismatches++;
    }
    if (expected_fast != 0) {
        int64_t direct = 0, hoisted = 0;
        depth_model(vertex, scale_q16, yaw, position_q16, camera_position_q16,
                    forward_q16, &direct, &hoisted);
        tally->model_cases++;
        if (direct != hoisted || direct != reference) tally->model_mismatches++;
    }
}

static const int16_t depth_sweep_vertices[][3] = {
    {0, 0, 0},
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
    {-1, -1, -1},
    {32767, 32767, 32767},
    {-32768, -32768, -32768},
    {32767, -32768, 0},
    {-32768, 32767, 0},
    {0, 32767, -32768},
    {100, -250, 700},
    {-1523, 811, -2044},
};

static const int32_t depth_sweep_scales[][3] = {
    {65536, 65536, 65536},
    {0, 0, 0},
    {1, 1, 1},
    {65535, 65535, 65535},
    {131072, 65536, 32768},
    {INT32_MAX, INT32_MAX, INT32_MAX},
    {INT32_MIN, INT32_MIN, INT32_MIN},
    {-65536, 65536, -65536},
};

static const int16_t depth_sweep_yaws[] = {
    0, 1, 0x1000, 0x2000, 0x3FFF, 0x4000, 0x7FFF, INT16_MIN,
};

static const int64_t depth_sweep_positions[][3] = {
    {0, 0, 0},
    {INT64_C(512) << 16, 0, -(INT64_C(900) << 16)},
    {-(INT64_C(1) << 20), INT64_C(1) << 20, INT64_C(1) << 20},
    {INT64_MAX, INT64_MAX, INT64_MAX},
    {INT64_MIN, INT64_MIN, INT64_MIN},
    {INT64_MAX / 2, INT64_MIN / 2, 0},
    {INT64_C(1) << 40, -(INT64_C(1) << 40), 0},
    {(INT64_C(1) << 40) + 1, 0, 0},
    {(int64_t)INT32_MAX * 65536, (int64_t)INT32_MIN * 65536, 0},
};

static const int32_t depth_sweep_cameras[][3] = {
    {0, 0, 0},
    {0, 100 << 16, -(300 << 16)},
    {INT32_MAX, INT32_MAX, INT32_MAX},
    {INT32_MIN, INT32_MIN, INT32_MIN},
    {INT32_MAX, INT32_MIN, 0},
    {-1, -1, -1},
    {1, 1, 1},
};

static const int32_t depth_sweep_forwards[][3] = {
    {0, 0, 65536},
    {0, 0, -65536},
    {65536, 0, 0},
    {0, 65536, 0},
    {46341, 0, 46341},
    {-46341, -46341, -46341},
    {INT32_MAX, 0, 0},
    {INT32_MIN, 0, 0},
    {0x100000, 0x100000, 0x100000},
};

static int depth_equivalence_sweep(void)
{
    depth_sweep_tally_t tally;
    uint64_t state = UINT64_C(0x5A7175726E543236);
    memset(&tally, 0, sizeof(tally));

    for (size_t v = 0U; v < sizeof(depth_sweep_vertices) /
             sizeof(depth_sweep_vertices[0]); v++)
    for (size_t s = 0U; s < sizeof(depth_sweep_scales) /
             sizeof(depth_sweep_scales[0]); s++)
    for (size_t y = 0U; y < sizeof(depth_sweep_yaws) /
             sizeof(depth_sweep_yaws[0]); y++)
    for (size_t p = 0U; p < sizeof(depth_sweep_positions) /
             sizeof(depth_sweep_positions[0]); p++)
    for (size_t c = 0U; c < sizeof(depth_sweep_cameras) /
             sizeof(depth_sweep_cameras[0]); c++)
    for (size_t f = 0U; f < sizeof(depth_sweep_forwards) /
             sizeof(depth_sweep_forwards[0]); f++)
        depth_check_case(depth_sweep_vertices[v], depth_sweep_scales[s],
                         depth_sweep_yaws[y], depth_sweep_positions[p],
                         depth_sweep_cameras[c], depth_sweep_forwards[f],
                         &tally);

    /* The domain that actually ships: real Mario pose vertices, unit scale,
     * BOB-scale world and camera coordinates, and a Q16 forward taken from the
     * same sine table the runtime uses. */
    for (uint32_t iteration = 0U; iteration < 250000U; iteration++) {
        const int32_t unit_scale[3] = {65536, 65536, 65536};
        const uint64_t draw = depth_rng(&state);
        const uint16_t index = (uint16_t)(draw % SM64_MARIO_VERTEX_COUNT);
        const int16_t *vertex = (draw & (UINT64_C(1) << 40)) != 0U
            ? sm64_mario_walking_animation_vertices[
                  (draw >> 20) % SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT][index]
            : sm64_mario_animation_vertices[
                  (draw >> 20) % SM64_MARIO_ANIMATION_FRAME_COUNT][index];
        const int16_t yaw = (int16_t)(depth_rng(&state) & 0xFFFFU);
        const int16_t pitch = (int16_t)(depth_rng(&state) & 0xFFFFU);
        const int64_t position_q16[3] = {
            ((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16,
            ((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16,
            ((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16,
        };
        const int32_t camera_position_q16[3] = {
            (int32_t)(((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16),
            (int32_t)(((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16),
            (int32_t)(((int64_t)(depth_rng(&state) % 16384U) - 8192) << 16),
        };
        const int32_t forward_q16[3] = {
            sm64_saturn_sins_q16(yaw),
            sm64_saturn_sins_q16(pitch),
            sm64_saturn_coss_q16(yaw),
        };
        depth_check_case(vertex, unit_scale, yaw, position_q16,
                         camera_position_q16, forward_q16, &tally);
    }

    if (tally.value_mismatches != 0U) {
        fprintf(stderr,
                "depth equivalence: %llu/%llu cases differ from the reference "
                "(worst |delta| %lld Q16, %llu cross an LOD tier, %llu cross a "
                "painter bin)\n",
                (unsigned long long)tally.value_mismatches,
                (unsigned long long)tally.cases,
                (long long)tally.worst_delta,
                (unsigned long long)tally.tier_mismatches,
                (unsigned long long)tally.bin_mismatches);
        return 0;
    }
    if (tally.model_mismatches != 0U) {
        fprintf(stderr,
                "depth equivalence: %llu/%llu in-domain cases break the "
                "per-actor hoist identity\n",
                (unsigned long long)tally.model_mismatches,
                (unsigned long long)tally.model_cases);
        return 0;
    }
    if (tally.domain_mismatches != 0U) {
        fprintf(stderr,
                "depth equivalence: %llu cases disagree about whether the "
                "fast kernel's preconditions hold\n",
                (unsigned long long)tally.domain_mismatches);
        return 0;
    }
    /* The divide-free kernel must actually be the path under test, or a sweep
     * that silently fell back everywhere would report a clean equivalence
     * while measuring the reference against itself. */
    if (tally.fast_cases != tally.model_cases) {
        fprintf(stderr,
                "depth equivalence: %llu fast-kernel cases against %llu "
                "in-domain cases\n",
                (unsigned long long)tally.fast_cases,
                (unsigned long long)tally.model_cases);
        return 0;
    }
    /* Non-vacuity: the sweep must really exercise both the hoisted domain and
     * the saturating fallback, or an all-fallback sweep would pass while
     * proving nothing about the replacement. */
    if (tally.model_cases < 250000U) {
        fprintf(stderr, "depth equivalence: only %llu in-domain cases swept\n",
                (unsigned long long)tally.model_cases);
        return 0;
    }
    if (tally.fallback_cases < 100000U) {
        fprintf(stderr, "depth equivalence: only %llu fallback cases swept\n",
                (unsigned long long)tally.fallback_cases);
        return 0;
    }
    printf("depth equivalence: %llu cases, %llu hoist-domain, %llu saturating "
           "fallback, 0 divergences\n",
           (unsigned long long)tally.cases,
           (unsigned long long)tally.model_cases,
           (unsigned long long)tally.fallback_cases);
    return 1;
}

/* ---------------------------------------------------------------------------
 * T2.6 step 1 -- the pass-1 -> pass-2 depth carry.
 *
 * The emission pass used to recompute actor_meshlet_live_depth_bounds() for
 * every meshlet from inputs pass 1 had already used; T2.5 measured the two
 * walks 0.001% apart.  The carry is a memoisation of identical inputs, so it
 * must be bit-identical by construction -- but "by construction" is an
 * argument, and this pins it as a measurement: the shipped entry point (which
 * serves pass 2 from the carry) and a test-only entry point that withholds the
 * carry (forcing the pre-T2.6 recomputation) must produce byte-identical
 * output for every case.
 * ------------------------------------------------------------------------- */
bool sm64_saturn_actor_meshlets_prepare_recompute(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats);

static uint64_t carry_case_hash(int carried, int32_t x, int32_t y, int32_t z,
                                int16_t yaw, uint16_t capacity,
                                const int16_t (*vertices)[3],
                                const int32_t forward_q16[3])
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
    memset(opaque, 0xC3, sizeof(opaque));
    memset(translucent, 0xC3, sizeof(translucent));
    memset(positions, 0xC3, sizeof(positions));
    memset(&stats, 0, sizeof(stats));
    snapshot.mario.position[0] = x;
    snapshot.mario.position[1] = y;
    snapshot.mario.position[2] = z;
    snapshot.mario.yaw = yaw;
    pose.vertices = vertices;
    for (int axis = 0; axis < 3; axis++)
        view.view_forward_q16[axis] = forward_q16[axis];
    accepted = carried
        ? (uint8_t)sm64_saturn_actor_meshlets_prepare(
              &snapshot, &pose, &view, &output, capacity, &stats)
        : (uint8_t)sm64_saturn_actor_meshlets_prepare_recompute(
              &snapshot, &pose, &view, &output, capacity, &stats);
    hash = hash_bytes(hash, &accepted, sizeof(accepted));
    hash = hash_bytes(hash, &output.opaque_count, sizeof(output.opaque_count));
    hash = hash_bytes(hash, &output.translucent_count,
                      sizeof(output.translucent_count));
    hash = hash_bytes(hash, &output.position_count,
                      sizeof(output.position_count));
    hash = hash_bytes(hash, opaque, sizeof(opaque));
    hash = hash_bytes(hash, translucent, sizeof(translucent));
    hash = hash_bytes(hash, positions, sizeof(positions));
    return hash_bytes(hash, &stats, sizeof(stats));
}

static int depth_carry_matches_recompute(void)
{
    static const uint16_t capacities[] = {
        SM64_MARIO_PRIMITIVE_COUNT, 1U, 64U, 400U,
    };
    uint64_t state = UINT64_C(0x43617272794132);
    uint32_t cases = 0U, admitted = 0U;

    for (uint32_t iteration = 0U; iteration < 4000U; iteration++) {
        const uint64_t draw = depth_rng(&state);
        const uint16_t frame = (uint16_t)((draw >> 8) & 0xFFFFU);
        /* Sweep all three pose banks: the neutral mesh and both animation
         * banks, because the carry is stamped with the pose vertex pointer. */
        const int16_t (*vertices)[3] =
            (draw & 3U) == 0U ? sm64_mario_vertices :
            (draw & 3U) == 1U
                ? sm64_mario_animation_vertices[
                      frame % SM64_MARIO_ANIMATION_FRAME_COUNT]
                : sm64_mario_walking_animation_vertices[
                      frame % SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT];
        const int16_t yaw = (int16_t)(depth_rng(&state) & 0xFFFFU);
        const int16_t pitch = (int16_t)(depth_rng(&state) & 0xFFFFU);
        const int32_t x = (int32_t)(depth_rng(&state) % 16384U) - 8192;
        const int32_t y = (int32_t)(depth_rng(&state) % 16384U) - 8192;
        /* Bias z across the LOD-tier thresholds (2048 / 4096 world units) and
         * the behind-camera cull, so the cases that actually change what pass
         * 2 emits are exercised rather than only distant ones. */
        const int32_t z = (int32_t)(depth_rng(&state) % 12288U) - 8192;
        const uint16_t capacity =
            capacities[depth_rng(&state) % (sizeof(capacities) /
                                            sizeof(capacities[0]))];
        const int32_t forward_q16[3] = {
            sm64_saturn_sins_q16(yaw),
            sm64_saturn_sins_q16(pitch) >> 2,
            sm64_saturn_coss_q16(yaw),
        };
        const uint64_t carried = carry_case_hash(1, x, y, z, yaw, capacity,
                                                 vertices, forward_q16);
        const uint64_t fresh = carry_case_hash(0, x, y, z, yaw, capacity,
                                               vertices, forward_q16);
        cases++;
        if (carried != fresh) {
            fprintf(stderr,
                    "depth carry: pass 2 diverged from a fresh recompute at "
                    "pos (%d,%d,%d) yaw %d capacity %u\n",
                    (int)x, (int)y, (int)z, (int)yaw, (unsigned)capacity);
            return 0;
        }
        if (carry_case_hash(1, x, y, z, yaw, SM64_MARIO_PRIMITIVE_COUNT,
                            vertices, forward_q16) !=
            carry_case_hash(0, x, y, z, yaw, SM64_MARIO_PRIMITIVE_COUNT,
                            vertices, forward_q16)) {
            fprintf(stderr, "depth carry: full-capacity emission diverged\n");
            return 0;
        }
        if (z < 0) admitted++;
    }
    /* Non-vacuity: an all-culled sweep would compare two empty outputs. */
    if (admitted < 1000U) {
        fprintf(stderr, "depth carry: only %u in-front-of-camera cases\n",
                (unsigned)admitted);
        return 0;
    }
    printf("depth carry: %u cases, carried emission == fresh recompute\n",
           (unsigned)cases);
    return 1;
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
    if (!depth_equivalence_sweep()) return 1;
    if (!depth_carry_matches_recompute()) return 1;
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
