#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/port/saturn/gfx/saturn_actor_bundle_runtime.h"

/* MinGW's CRT assertion can open a modal dialog in noninteractive gates.
 * Keep every predicate active while making failure deterministic and visible. */
#undef assert
#define assert(predicate) do { \
    if (!(predicate)) { \
        fprintf(stderr, "actor bundle runtime assertion failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #predicate); \
        exit(1); \
    } \
} while (0)

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint8_t *read_file(const char *path, uint32_t *byte_count)
{
    FILE *file = fopen(path, "rb");
    uint8_t *bytes;
    long size;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || fread(bytes, 1U, (size_t)size, file) !=
                             (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *byte_count = (uint32_t)size;
    return bytes;
}

static void first_snapshot(const sm64_saturn_actor_bundle_view_t *bundle,
                           uint32_t residency_generation,
                           sm64_saturn_actor_instance_snapshot_t *snapshot)
{
    const uint8_t *record = bundle->bytes + bundle->variant_records_offset;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->generation = 91U;
    snapshot->scene_package_generation = residency_generation;
    snapshot->family_id = read_be16(record);
    snapshot->model_id = read_be16(record + 2U);
    snapshot->actor_bank_id = read_be32(record + 56U);
    for (uint16_t word = 0U; word < 8U; word++)
        snapshot->actor_bank_hash_words[word] =
            read_be32(record + 56U + (uint32_t)word * 4U);
    snapshot->active = 1U;
    snapshot->render_active = 1U;
    snapshot->scale_q16[0] = 1 << 16;
    snapshot->scale_q16[1] = 1 << 16;
    snapshot->scale_q16[2] = 1 << 16;
}

int main(int argc, char **argv)
{
    sm64_saturn_actor_bundle_publication_t publication;
    sm64_saturn_actor_bundle_publication_t before;
    sm64_saturn_actor_bundle_view_t bundle;
    sm64_saturn_actor_bundle_resolution_t resolution[2];
    sm64_saturn_actor_instance_snapshot_t snapshot;
    sm64_saturn_actor_output_record_t *records;
    uint8_t *bundle_bytes;
    uint8_t *workspace_storage;
    uint32_t bundle_size = 0U;
    const uint32_t residency_generation = 17U;

    if (argc != 2) return 2;
    bundle_bytes = read_file(argv[1], &bundle_size);
    records = (sm64_saturn_actor_output_record_t *)calloc(
        256U, sizeof(*records));
    workspace_storage = (uint8_t *)malloc(1280U + 4U);
    assert(bundle_bytes != NULL && records != NULL &&
           workspace_storage != NULL);
    assert(sm64_saturn_actor_bundle_validate(bundle_bytes, bundle_size,
                                              &bundle));
    assert(bundle.maximum_scratch == 1091U);
    assert(bundle.workspace_lane_stride != 0U);

    memset(&publication, 0xA5, sizeof(publication));
    assert(sm64_saturn_actor_bundle_runtime_publish(
        &publication, &bundle, residency_generation, 0x00100000U));
    assert(publication.committed == 1U);
    assert(publication.residency_generation == residency_generation);
    assert(publication.package_generation == bundle.package_generation);
    assert(publication.cart_offset == 0x00100000U);
    assert(publication.byte_count == bundle.byte_count);
    assert(publication.family_count == bundle.family_count);
    assert(publication.variant_count == bundle.variant_count);
    assert(publication.workspace_lane_stride == bundle.workspace_lane_stride);
    assert(publication.maximum_scratch == bundle.maximum_scratch);
    assert(memcmp(publication.content_hash_words, bundle.content_hash_words,
                  sizeof(publication.content_hash_words)) == 0);

    before = publication;
    assert(!sm64_saturn_actor_bundle_runtime_publish(
        &publication, &bundle, residency_generation, 0x00100000U));
    assert(memcmp(&publication, &before, sizeof(publication)) == 0);

    first_snapshot(&bundle, residency_generation, &snapshot);
    assert(sm64_saturn_actor_bundle_runtime_claim(
        &publication, residency_generation, 0U));
    assert(sm64_saturn_actor_bundle_runtime_claim(
        &publication, residency_generation, 1U));
    assert(!sm64_saturn_actor_bundle_runtime_claim(
        &publication, residency_generation, 0U));
    assert(!sm64_saturn_actor_bundle_runtime_claim(
        &publication, residency_generation - 1U, 0U));

    assert(sm64_saturn_actor_bundle_variant(
        &bundle, snapshot.family_id, snapshot.model_id,
        &resolution[0].variant));
    assert(sm64_saturn_actor_bundle_resolve(
        &bundle, snapshot.family_id, snapshot.model_id,
        snapshot.actor_bank_id, snapshot.actor_bank_hash_words,
        &resolution[0].bank));
    {
        uint32_t lane_bytes, usable_bytes, reserved_bytes;
        const uintptr_t scratch_start = (uintptr_t)workspace_storage;
        const uintptr_t scratch_end = scratch_start + 1091U;
        const uintptr_t records_start = (uintptr_t)records;
        const uintptr_t records_end = records_start + 128U * sizeof(*records);
        assert(sm64_saturn_actor_meshlets_workspace_query(
            &resolution[0].bank, &lane_bytes, &usable_bytes,
            &reserved_bytes));
        assert(lane_bytes == 424U && usable_bytes == 848U &&
               reserved_bytes == 851U);
        assert(bundle.workspace_lane_stride >= lane_bytes);
        assert((bundle.workspace_lane_stride & 3U) == 0U);
        assert(1280U >= 2U * bundle.workspace_lane_stride + 3U);
        assert(!(scratch_start < records_end && records_start < scratch_end));
        assert((uint32_t)resolution[0].bank.bank.vertex_count * 9U +
                   (uint32_t)resolution[0].bank.bank.joint_count * 64U <=
               bundle.workspace_lane_stride);
    }
    if (!sm64_saturn_actor_meshlets_bind_bundle_workspace(
            &resolution[0].bank, workspace_storage, 1280U,
            bundle.workspace_lane_stride, 0U, records, 128U,
            &resolution[0].workspace)) {
        fprintf(stderr,
                "bundle workspace bind failed: scratch=%p records=%p "
                "stride=%lu bank-lane=%lu bank-scratch=%lu\n",
                (void *)workspace_storage, (void *)records,
                (unsigned long)bundle.workspace_lane_stride,
                (unsigned long)resolution[0].variant.bank_lane_bytes,
                (unsigned long)resolution[0].bank.max_scratch);
        fprintf(stderr, "bank vertices=%u joints=%u primitives=%u\n",
                resolution[0].bank.bank.vertex_count,
                resolution[0].bank.bank.joint_count,
                resolution[0].bank.bank.primitive_count);
        return 1;
    }

    for (uint8_t residue = 0U; residue < 4U; residue++) {
        uint8_t *workspace = workspace_storage + residue;
        memset(resolution, 0xA5, sizeof(resolution));
        assert(sm64_saturn_actor_bundle_runtime_resolve(
            &publication, &bundle, workspace, 1280U, &snapshot, 0U,
            records, 128U, &resolution[0]));
        assert(sm64_saturn_actor_bundle_runtime_resolve(
            &publication, &bundle, workspace, 1280U, &snapshot, 1U,
            records + 128U, 128U, &resolution[1]));
        assert(resolution[0].residency_generation == residency_generation);
        assert(resolution[0].package_generation == bundle.package_generation);
        assert(resolution[0].lane == 0U && resolution[1].lane == 1U);
        assert(resolution[0].workspace.scratch_offset +
                   resolution[0].workspace.scratch_size <=
               resolution[1].workspace.scratch_offset);
        assert(resolution[1].workspace.scratch_offset +
                   resolution[1].workspace.scratch_size <= 1280U);
        assert((((uintptr_t)workspace +
                     resolution[0].workspace.scratch_offset) & 3U) == 0U);
        assert((((uintptr_t)workspace +
                     resolution[1].workspace.scratch_offset) & 3U) == 0U);
    }

    memset(&resolution[0], 0xA5, sizeof(resolution[0]));
    assert(!sm64_saturn_actor_bundle_runtime_resolve(
        &publication, &bundle, workspace_storage, 1090U, &snapshot, 0U,
        records, 128U, &resolution[0]));
    assert(memcmp(&resolution[0],
                  &(sm64_saturn_actor_bundle_resolution_t){0},
                  sizeof(resolution[0])) == 0);
    snapshot.scene_package_generation++;
    assert(!sm64_saturn_actor_bundle_runtime_resolve(
        &publication, &bundle, workspace_storage, 1280U, &snapshot, 0U,
        records, 128U, &resolution[0]));
    snapshot.scene_package_generation--;

    assert(!sm64_saturn_actor_bundle_runtime_release(
        &publication, residency_generation - 1U, 0U));
    assert(sm64_saturn_actor_bundle_runtime_release(
        &publication, residency_generation, 0U));
    assert(sm64_saturn_actor_bundle_runtime_release(
        &publication, residency_generation, 1U));
    assert(!sm64_saturn_actor_bundle_runtime_release(
        &publication, residency_generation, 1U));

    before = publication;
    before.content_hash_words[0] ^= 1U;
    memset(&resolution[0], 0xA5, sizeof(resolution[0]));
    assert(sm64_saturn_actor_bundle_runtime_claim(
        &before, residency_generation, 0U));
    assert(!sm64_saturn_actor_bundle_runtime_resolve(
        &before, &bundle, workspace_storage, 1280U, &snapshot, 0U,
        records, 128U, &resolution[0]));
    assert(sm64_saturn_actor_bundle_runtime_release(
        &before, residency_generation, 0U));

    free(bundle_bytes);
    free(records);
    free(workspace_storage);
    puts("actor bundle runtime: PASS");
    return 0;
}
