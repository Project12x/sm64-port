#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yaul.h>

#include "../../src/port/saturn/sourceboot/source_scene_bundle.h"
#include "../../src/port/saturn/gpl/slavedriver_dma_queue.h"
#include "../../src/port/saturn/gfx/saturn_texture_residency.h"
#include "../../src/port/saturn/runtime/saturn_scene_package.h"

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

static uint8_t *fixture_find(uint8_t *bytes, uint32_t byte_count,
                             const uint8_t *needle, uint32_t needle_bytes)
{
    uint32_t offset;
    if (needle_bytes == 0U || needle_bytes > byte_count) return NULL;
    for (offset = 0U; offset <= byte_count - needle_bytes; offset++)
        if (memcmp(bytes + offset, needle, needle_bytes) == 0)
            return bytes + offset;
    return NULL;
}

static bool fixture_reseal_root(uint8_t *root, uint32_t root_bytes)
{
    uint8_t digest[32];
    uint8_t dependency_canonical[81] = "S64P-DEPS\0\1";
    uint8_t *dependency;
    uint32_t descriptor_offset = 0U, section_offset = 0U, section_bytes = 0U;
    uint16_t section_index, section_count;
    if (root == NULL || root_bytes < 148U)
        return false;
    section_count = fixture_read_be16(root + 16U);
    for (section_index = 0U; section_index < section_count; section_index++) {
        const uint32_t candidate = 84U + (uint32_t)section_index * 64U;
        if (candidate > root_bytes || 64U > root_bytes - candidate)
            return false;
        if (fixture_read_be16(root + candidate) == 5U) {
            descriptor_offset = candidate;
            section_offset = fixture_read_be32(root + candidate + 8U);
            section_bytes = fixture_read_be32(root + candidate + 12U);
            break;
        }
    }
    if (descriptor_offset == 0U)
        return false;
    if (section_offset > root_bytes || section_bytes > root_bytes - section_offset ||
        section_bytes < 100U)
        return false;
    dependency = root + section_offset + 4U;
    if (!sm64_saturn_scene_package_sha256(
            root + section_offset, section_bytes, digest))
        return false;
    memcpy(root + descriptor_offset + 28U, digest, 32U);
    dependency_canonical[11] = dependency[0];
    dependency_canonical[12] = dependency[1];
    memcpy(dependency_canonical + 13U, dependency + 4U, 32U);
    memcpy(dependency_canonical + 45U, dependency + 84U, 4U);
    memcpy(dependency_canonical + 49U, dependency + 52U, 32U);
    if (!sm64_saturn_scene_package_sha256(
            dependency_canonical, sizeof(dependency_canonical), digest))
        return false;
    memcpy(root + 52U, digest, 32U);
    memset(root + 20U, 0, 32U);
    if (!sm64_saturn_scene_package_sha256(root, root_bytes, digest))
        return false;
    memcpy(root + 20U, digest, 32U);
    return true;
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
    vdp1_vram_partitions_t actor_partitions;
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

    {
        static const uint8_t stable_id[32] = "bob-area1-actors-v3";
        uint8_t *id = fixture_find(root, root_bytes, stable_id,
                                   sizeof(stable_id));
        CHECK(id != NULL);
        id[0] = (uint8_t)'x';
        CHECK(fixture_reseal_root(root, root_bytes));
        CHECK(!sm64_saturn_source_scene_bundle_init_from(
            root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
            &partitions));
        id[0] = (uint8_t)'b';
        CHECK(fixture_reseal_root(root, root_bytes));
        id[-1] = 3U;
        CHECK(fixture_reseal_root(root, root_bytes));
        CHECK(!sm64_saturn_source_scene_bundle_init_from(
            root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
            &partitions));
        id[-1] = 2U;
        CHECK(fixture_reseal_root(root, root_bytes));
    }

    initialized = sm64_saturn_source_scene_bundle_init_from(
        root, root_bytes, bundle, bundle_bytes, 9U, 1U, 0x10000U,
        &partitions);
    if (!initialized) {
        probe = sm64_saturn_source_scene_bundle_probe();
        fprintf(stderr, "source scene bundle initialization status: %u\n",
                probe == NULL ? UINT32_MAX : probe->status);
    }
    CHECK(initialized);
    CHECK(sm64_saturn_actor_bundle_validate(
        bundle, bundle_bytes, &bundle_view));
    probe = sm64_saturn_source_scene_bundle_probe();
    CHECK(probe != NULL && probe->status ==
          SM64_SATURN_SOURCE_SCENE_BUNDLE_READY);
    CHECK(probe->residency_generation == 1U &&
          probe->package_generation == expected_package_generation);
    CHECK(probe->root_bytes == root_bytes &&
          probe->bundle_bytes == bundle_bytes &&
          probe->variant_count == bundle_view.variant_count);
    textures = sm64_saturn_source_scene_bundle_textures(1U);
    CHECK(textures != NULL && textures->committed == 1U &&
          textures->generation == 1U &&
          textures->mapping_count == bundle_view.variant_count);
    memset(&actor_partitions, 0xA5, sizeof(actor_partitions));
    CHECK(!sm64_saturn_source_scene_bundle_texture_partitions(
        0U, &actor_partitions));
    CHECK(sm64_saturn_source_scene_bundle_texture_partitions(
        1U, &actor_partitions));
    CHECK(actor_partitions.texture_base == partitions.texture_base &&
          actor_partitions.texture_size == partitions.texture_size &&
          actor_partitions.clut_base == partitions.clut_base &&
          actor_partitions.clut_size == partitions.clut_size);
    {
        const uint8_t *variant =
            bundle_view.bytes + bundle_view.variant_records_offset;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.generation = 1U;
        snapshot.scene_package_generation = expected_package_generation;
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

    /* Exact first visible generic actor observed by the identity-bound BOB
     * target.  This is the earliest live consumer, not a synthetic family-0
     * fixture: if its package identity, pose, camera, or meshlet preparation
     * drifts, the normal frame fails before any actor reaches VDP1. */
    {
        sm64_saturn_actor_bundle_variant_t cannon;
        CHECK(sm64_saturn_actor_bundle_variant(
            &bundle_view, 29U, 128U, &cannon));
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.generation = 1U;
        snapshot.scene_package_generation = expected_package_generation;
        snapshot.instance_key = 65560U;
        snapshot.actor_bank_id = cannon.source_hash_words[0];
        memcpy(snapshot.actor_bank_hash_words, cannon.source_hash_words,
               sizeof(snapshot.actor_bank_hash_words));
        snapshot.family_id = cannon.family_ordinal;
        snapshot.model_id = cannon.model_id;
        snapshot.parent_index = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
        snapshot.parent_node_ordinal = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
        snapshot.position_q16[0] = 90963968;
        snapshot.position_q16[1] = 191234048;
        snapshot.position_q16[2] = -156172288;
        snapshot.scale_q16[0] = 65536;
        snapshot.scale_q16[1] = 65536;
        snapshot.scale_q16[2] = 65536;
        snapshot.angle[1] = -8192;
        snapshot.animation_id = 0;
        snapshot.animation_frame = 0;
        snapshot.opacity = 255U;
        snapshot.active = 1U;
        snapshot.render_active = 1U;
        CHECK(sm64_saturn_source_scene_bundle_resolve(
            &snapshot, 0U, records, 128U, &resolution));
        memset(&render_view, 0, sizeof(render_view));
        memset(&stats, 0, sizeof(stats));
        render_view.camera_position_q16[0] = -107413504;
        render_view.camera_position_q16[1] = 100270080;
        render_view.camera_position_q16[2] = 413925376;
        render_view.camera_focus_q16[0] = 43515904;
        render_view.camera_focus_q16[1] = 1966080;
        render_view.camera_focus_q16[2] = -120586240;
        render_view.view_forward_q16[0] = 17537;
        render_view.view_forward_q16[1] = -11422;
        render_view.view_forward_q16[2] = -62109;
        render_view.generation = snapshot.generation;
        CHECK(sm64_saturn_actor_meshlets_prepare_bank(
            &resolution.bank, &snapshot, &render_view,
            &resolution.workspace.pose_work, &resolution.workspace.output,
            &stats));
        CHECK((uint32_t)resolution.workspace.output.output.opaque_count +
                  resolution.workspace.output.output.translucent_count != 0U);
        CHECK(sm64_saturn_source_scene_bundle_release(0U, 1U));
    }
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
