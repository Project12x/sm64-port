#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct vdp1_vram_partitions {
    void *cmdt_base;
    uint32_t cmdt_size;
    void *texture_base;
    uint32_t texture_size;
    void *gouraud_base;
    uint32_t gouraud_size;
    void *clut_base;
    uint32_t clut_size;
    void *remaining_base;
    uint32_t remaining_size;
} vdp1_vram_partitions_t;

#include "../../src/port/saturn/sourceboot/source_scene_bundle.h"
#include "../../src/port/saturn/gpl/slavedriver_dma_queue.h"
#include "../../src/port/saturn/gfx/saturn_texture_residency.h"

#define SM64_SATURN_ACTOR_TEXTURE_RESIDENCY_HOST_TEST 1

#define CHECK(predicate) do { if (!(predicate)) { \
    fprintf(stderr, "source scene bundle assertion failed: %s:%d: %s\n", \
            __FILE__, __LINE__, #predicate); return 1; \
} } while (0)

void sm64_saturn_texture_residency_init_region(
    sm64_saturn_texture_residency_t *residency, void *base, size_t capacity)
{
    residency->base = base;
    residency->capacity = capacity;
    residency->used = 0U;
    residency->peak = 0U;
    residency->overflowed = false;
}

int saturn_dma_queue_request_valid(void *destination, const void *source,
                                   size_t bytes,
                                   saturn_dma_queue_mode_t mode)
{
    return destination != NULL && source != NULL && bytes != 0U &&
        mode == SATURN_DMA_QUEUE_SCU;
}

saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *destination, const void *source, size_t bytes,
    saturn_dma_queue_mode_t mode)
{
    static saturn_dma_queue_sequence_t sequence;
    if (!saturn_dma_queue_request_valid(destination, source, bytes, mode))
        return SATURN_DMA_QUEUE_SEQUENCE_INVALID;
    memcpy(destination, source, bytes);
    return ++sequence;
}

int saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
    return sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID;
}

#include "../../src/port/saturn/gfx/saturn_actor_texture_residency.c"

static uint8_t *read_file(const char *path, uint32_t *byte_count)
{
    FILE *file = fopen(path, "rb");
    uint8_t *bytes;
    long size;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0L || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)size);
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

static uint16_t fixture_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t fixture_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

int main(int argc, char **argv)
{
    _Alignas(32) static uint8_t texture_vram[20000];
    _Alignas(32) static uint8_t clut_vram[4096];
    vdp1_vram_partitions_t partitions = {0};
    uint8_t *root, *bundle;
    uint32_t root_bytes = 0U, bundle_bytes = 0U;
    const sm64_saturn_source_scene_bundle_probe_t *probe;
    const sm64_saturn_actor_texture_publication_t *textures;
    sm64_saturn_actor_bundle_view_t bundle_view;
    sm64_saturn_actor_bundle_resolution_t resolution;
    sm64_saturn_actor_instance_snapshot_t snapshot;
    sm64_saturn_actor_output_record_t *records;
    sm64_saturn_render_view_t render_view;
    sm64_saturn_fast3d_profile_t stats;
    uint32_t expected_package_generation;
    uint8_t saved;
    bool initialized;

    if (argc != 4) return 2;
    expected_package_generation = (uint32_t)strtoul(argv[3], NULL, 10);
    CHECK(expected_package_generation != 0U);
    root = read_file(argv[1], &root_bytes);
    bundle = read_file(argv[2], &bundle_bytes);
    CHECK(root != NULL && bundle != NULL);
    partitions.texture_base = texture_vram;
    partitions.texture_size = sizeof(texture_vram);
    partitions.clut_base = clut_vram;
    partitions.clut_size = sizeof(clut_vram);

    saved = root[20];
    root[20] ^= 1U;
    CHECK(!sm64_saturn_source_scene_bundle_init_from(
        root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
        &partitions));
    root[20] = saved;

    initialized = sm64_saturn_source_scene_bundle_init_from(
        root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
        &partitions);
    if (!initialized) {
        probe = sm64_saturn_source_scene_bundle_probe();
        fprintf(stderr, "source scene bundle initialization status: %u\n",
                probe == NULL ? UINT32_MAX : probe->status);
    }
    CHECK(initialized);
    probe = sm64_saturn_source_scene_bundle_probe();
    CHECK(probe != NULL && probe->status ==
          SM64_SATURN_SOURCE_SCENE_BUNDLE_READY);
    CHECK(probe->residency_generation == 1U &&
          probe->package_generation == expected_package_generation);
    CHECK(probe->root_bytes == root_bytes &&
          probe->bundle_bytes == bundle_bytes && probe->variant_count == 14U);
    textures = sm64_saturn_source_scene_bundle_textures(1U);
    CHECK(textures != NULL && textures->committed == 1U &&
          textures->generation == 1U && textures->mapping_count == 14U);
    CHECK(sm64_saturn_actor_bundle_validate(
        bundle, bundle_bytes, &bundle_view));
    {
        const uint8_t *variant =
            bundle_view.bytes + bundle_view.variant_records_offset;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.generation = 1U;
        snapshot.scene_package_generation = 1U;
        snapshot.family_id = fixture_read_be16(variant);
        snapshot.model_id = fixture_read_be16(variant + 2U);
        snapshot.actor_bank_id = fixture_read_be32(variant + 56U);
        for (uint16_t word = 0U; word < 8U; word++)
            snapshot.actor_bank_hash_words[word] =
                fixture_read_be32(variant + 56U + (uint32_t)word * 4U);
        snapshot.active = 1U;
        snapshot.render_active = 1U;
        snapshot.position_q16[2] = 512 << 16;
        snapshot.scale_q16[0] = 1;
        snapshot.scale_q16[1] = 1;
        snapshot.scale_q16[2] = 1;
    }
    records = calloc(128U, sizeof(*records));
    CHECK(records != NULL);
    CHECK(sm64_saturn_source_scene_bundle_resolve(
        &snapshot, 0U, records, 128U, &resolution));
    CHECK(resolution.variant.family_ordinal == snapshot.family_id &&
          resolution.variant.model_id == snapshot.model_id &&
          resolution.bank.bank.family_id == snapshot.family_id &&
          resolution.bank.bank.model_id == snapshot.model_id);
    memset(&render_view, 0, sizeof(render_view));
    memset(&stats, 0, sizeof(stats));
    render_view.generation = snapshot.generation;
    render_view.view_forward_q16[2] = 1 << 16;
    CHECK(sm64_saturn_actor_meshlets_prepare_bank(
        &resolution.bank, &snapshot, &render_view,
        &resolution.workspace.pose_work, &resolution.workspace.output,
        &stats));
    CHECK((uint32_t)resolution.workspace.output.output.opaque_count +
              resolution.workspace.output.output.translucent_count != 0U &&
          (uint32_t)resolution.workspace.output.output.opaque_count +
              resolution.workspace.output.output.translucent_count <=
                  resolution.bank.bank.primitive_count);
    CHECK(resolution.workspace.output.output.position_count != 0U &&
          resolution.workspace.output.quarantine_reason ==
              SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE);
    CHECK(sm64_saturn_source_scene_bundle_release(0U, 1U));
    CHECK(sm64_saturn_source_scene_bundle_step(true));
    CHECK(!sm64_saturn_source_scene_bundle_init_from(
        root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
        &partitions));

    free(records);
    free(root);
    free(bundle);
    puts("source scene bundle: PASS");
    return 0;
}
