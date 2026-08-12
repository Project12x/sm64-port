#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Complete the tagged Yaul partition type before including the production
 * source. The public actor-residency header deliberately forward-declares this
 * hardware type so scene_residency.h stays consumable without Yaul headers. */
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

#include "../../src/port/saturn/gpl/slavedriver_dma_queue.h"
#include "../../src/port/saturn/runtime/saturn_sha256.h"
#include "../../src/port/saturn/gfx/saturn_texture_residency.h"

void sm64_saturn_texture_residency_init_region(
    sm64_saturn_texture_residency_t *residency, void *base, size_t capacity)
{
    assert(residency != NULL);
    residency->base = base;
    residency->capacity = capacity;
    residency->used = 0U;
    residency->peak = 0U;
    residency->overflowed = false;
}

static const void *g_observed_publication;
static uint32_t g_submit_count;
static uint32_t g_wait_count;
static uint32_t g_fail_submit;
static uint32_t g_fail_wait;

/* These checked queue doubles copy real fixture bytes and observe the real
 * publication during every transfer boundary. They replace only Saturn DMA
 * hardware; bundle/bank parsing and residency logic remain production code. */
saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *destination, const void *source, size_t bytes,
    saturn_dma_queue_mode_t mode)
{
    const volatile uint8_t *committed = g_observed_publication == NULL ? NULL :
        (const volatile uint8_t *)g_observed_publication + 2062U;
    const uint32_t *generation = g_observed_publication == NULL ? NULL :
        (const uint32_t *)((const uint8_t *)g_observed_publication + 2056U);
    g_submit_count++;
    assert(mode == SATURN_DMA_QUEUE_SCU);
    assert(destination != NULL && source != NULL && bytes != 0U);
    assert(committed == NULL || (*committed == 0U && *generation == 0U));
    if (g_submit_count == g_fail_submit)
        return SATURN_DMA_QUEUE_SEQUENCE_INVALID;
    memcpy(destination, source, bytes);
    return g_submit_count;
}

int saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
    const volatile uint8_t *committed = g_observed_publication == NULL ? NULL :
        (const volatile uint8_t *)g_observed_publication + 2062U;
    const uint32_t *generation = g_observed_publication == NULL ? NULL :
        (const uint32_t *)((const uint8_t *)g_observed_publication + 2056U);
    g_wait_count++;
    assert(sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(committed == NULL || (*committed == 0U && *generation == 0U));
    return g_wait_count != g_fail_wait;
}

#include "../../src/port/saturn/gfx/saturn_actor_texture_residency.c"

_Static_assert(SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ==
               SM64_SATURN_ACTOR_BUNDLE_MAX_VARIANTS,
               "every valid S64F variant must fit the fixed mapping table");
_Static_assert(sizeof(sm64_saturn_actor_texture_mapping_t) == 16U,
               "mapping ABI must remain scalar-only");
_Static_assert(offsetof(sm64_saturn_actor_texture_mapping_t, bank_id) == 0U &&
               offsetof(sm64_saturn_actor_texture_mapping_t,
                        texture_base_offset) == 4U &&
               offsetof(sm64_saturn_actor_texture_mapping_t,
                        clut_base_index) == 8U &&
               offsetof(sm64_saturn_actor_texture_mapping_t, tile_count) == 10U &&
               offsetof(sm64_saturn_actor_texture_mapping_t, generation) == 12U,
               "Task 6 mapping field layout changed");
_Static_assert(sizeof(sm64_saturn_actor_texture_publication_t) == 2064U,
               "publication HWRAM footprint must stay fixed and pointer-free");
_Static_assert(offsetof(sm64_saturn_actor_texture_publication_t, generation) ==
               2056U &&
               offsetof(sm64_saturn_actor_texture_publication_t, committed) ==
               2062U,
               "generation/commit suffix must remain explicit");

typedef struct fixture {
    uint8_t *bytes;
    uint32_t size;
    sm64_saturn_actor_bundle_view_t view;
} fixture_t;

typedef struct aligned_vram {
    _Alignas(32) uint8_t texture[20000];
    _Alignas(32) uint8_t clut[4096];
} aligned_vram_t;

static uint16_t be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static fixture_t load_fixture(const char *path)
{
    fixture_t result;
    FILE *file = fopen(path, "rb");
    long size;
    assert(file != NULL);
    assert(fseek(file, 0L, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0L && (unsigned long)size <= UINT32_MAX);
    assert(fseek(file, 0L, SEEK_SET) == 0);
    result.bytes = malloc((size_t)size);
    assert(result.bytes != NULL);
    assert(fread(result.bytes, 1U, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    result.size = (uint32_t)size;
    assert(sm64_saturn_actor_bundle_validate(result.bytes, result.size,
                                              &result.view));
    return result;
}

static void reset_dma(sm64_saturn_actor_texture_publication_t *publication,
                      uint32_t fail_submit, uint32_t fail_wait)
{
    g_observed_publication = publication;
    g_submit_count = 0U;
    g_wait_count = 0U;
    g_fail_submit = fail_submit;
    g_fail_wait = fail_wait;
}

static vdp1_vram_partitions_t partitions_for(aligned_vram_t *vram,
                                              uint32_t texture_size,
                                              uint32_t clut_size)
{
    vdp1_vram_partitions_t partitions = {0};
    partitions.texture_base = vram->texture;
    partitions.texture_size = texture_size;
    partitions.clut_base = vram->clut;
    partitions.clut_size = clut_size;
    return partitions;
}

static void assert_invalid(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation, uint32_t bank_id)
{
    sm64_saturn_actor_texture_mapping_t output;
    memset(&output, 0xA5, sizeof(output));
    assert(publication->committed == 0U);
    assert(publication->generation == 0U);
    assert(publication->mapping_count == 0U);
    assert(publication->texture_bytes == 0U);
    assert(publication->clut_bytes == 0U);
    assert(!sm64_saturn_actor_texture_residency_lookup(
        publication, generation, bank_id, &output));
    assert(memcmp(&output, &(sm64_saturn_actor_texture_mapping_t){0},
                  sizeof(output)) == 0);
}

static bool resolve_variant(const sm64_saturn_actor_bundle_view_t *view,
                            uint16_t index,
                            sm64_saturn_actor_bundle_variant_t *variant,
                            sm64_saturn_actor_bank_view_t *bank)
{
    const uint8_t *record = view->bytes + view->variant_records_offset +
        (uint32_t)index * SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
    uint32_t source[8];
    uint16_t word;
    for (word = 0U; word < 8U; word++)
        source[word] = be32(record + 56U + (uint32_t)word * 4U);
    if (!sm64_saturn_actor_bundle_variant(
            view, be16(record), be16(record + 2U), variant))
        return false;
    return sm64_saturn_actor_bundle_resolve(
        view, variant->family_ordinal, variant->model_id, source[0], source,
        bank);
}

static void assert_canonical_publication(
    const fixture_t *fixture, const aligned_vram_t *vram,
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t residency_generation,
    uint16_t expected_mappings, uint32_t expected_texture,
    uint32_t expected_clut, bool require_cannon)
{
    uint32_t texture_cursor = 0U, clut_cursor = 0U;
    uint16_t selected = 0U, index;
    bool cannon = false;
    assert(publication->committed == 1U);
    assert(publication->generation == residency_generation);
    assert(publication->mapping_count == expected_mappings);
    assert(publication->texture_bytes == expected_texture);
    assert(publication->clut_bytes == expected_clut);
    for (index = 0U; index < fixture->view.variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        sm64_saturn_actor_texture_mapping_t mapping;
        assert(resolve_variant(&fixture->view, index, &variant, &bank));
        if (bank.bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2) {
            assert(!sm64_saturn_actor_texture_residency_lookup(
                publication, residency_generation,
                bank.bank.source_hash_words[0], &mapping));
            continue;
        }
        assert(sm64_saturn_actor_texture_residency_lookup(
            publication, residency_generation,
            bank.bank.source_hash_words[0], &mapping));
        assert(mapping.bank_id == bank.bank.source_hash_words[0]);
        assert(mapping.texture_base_offset == texture_cursor);
        assert(mapping.clut_base_index == clut_cursor / 32U);
        assert(mapping.tile_count == bank.tile_count);
        assert(mapping.generation == residency_generation);
        {
            uint16_t prior;
            for (prior = 0U; prior < selected; prior++)
                assert(publication->mappings[prior].bank_id != mapping.bank_id);
        }
        assert(memcmp(vram->texture + texture_cursor,
                      bank.bytes + bank.texture_payload_offset,
                      bank.texture_resident_bytes) == 0);
        assert(memcmp(vram->clut + clut_cursor,
                      bank.bytes + bank.clut_payload_offset,
                      bank.clut_resident_bytes) == 0);
        texture_cursor += bank.texture_resident_bytes;
        clut_cursor += bank.clut_resident_bytes;
        selected++;
        if (variant.family_ordinal == 29U && variant.model_id == 0x0080U)
            cannon = true;
    }
    assert(selected == expected_mappings);
    assert(texture_cursor == expected_texture && clut_cursor == expected_clut);
    assert(!require_cannon || cannon);
}

static void test_mixed_and_real_success(const fixture_t *mixed,
                                        const fixture_t *real)
{
    aligned_vram_t vram;
    sm64_saturn_actor_texture_publication_t publication = {0};
    vdp1_vram_partitions_t partitions;
    sm64_saturn_actor_texture_mapping_t mapping;
    const uint32_t mixed_generation = mixed->view.package_generation + 101U;
    const uint32_t real_generation = real->view.package_generation + 202U;

    memset(&vram, 0xCC, sizeof(vram));
    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    reset_dma(&publication, 0U, 0U);
    assert(sm64_saturn_actor_texture_residency_activate(
        &publication, &mixed->view, &partitions,
        mixed_generation, true, true));
    assert(mixed_generation != mixed->view.package_generation);
    assert_canonical_publication(mixed, &vram, &publication, mixed_generation,
                                 1U, 16U, 32U, false);
    assert(g_submit_count == 2U && g_wait_count == 2U);
    assert(!sm64_saturn_actor_texture_residency_lookup(
        &publication, 0U, publication.mappings[0].bank_id, &mapping));
    assert(!sm64_saturn_actor_texture_residency_lookup(
        &publication, mixed->view.package_generation,
        publication.mappings[0].bank_id, &mapping));
    assert(!sm64_saturn_actor_texture_residency_lookup(
        &publication, mixed_generation,
        publication.mappings[0].bank_id ^ 1U, &mapping));

    memset(&publication, 0, sizeof(publication));
    memset(&vram, 0xCC, sizeof(vram));
    reset_dma(&publication, 0U, 0U);
    assert(sm64_saturn_actor_texture_residency_activate(
        &publication, &real->view, &partitions,
        real_generation, true, true));
    assert_canonical_publication(real, &vram, &publication, real_generation,
                                 14U, 16640U, 2816U, true);
}

static void test_lifecycle_and_prior_state_fail_closed(const fixture_t *fixture)
{
    aligned_vram_t vram;
    vdp1_vram_partitions_t partitions = partitions_for(
        &vram, sizeof(vram.texture), sizeof(vram.clut));
    sm64_saturn_actor_texture_publication_t publication;
    const uint32_t generation = fixture->view.package_generation + 101U;
    const uint32_t bank_id = be32(fixture->view.bytes +
        fixture->view.variant_records_offset + 56U);

    memset(&publication, 0, sizeof(publication));
    publication.committed = 1U;
    publication.generation = generation - 1U;
    publication.mapping_count = 1U;
    publication.mappings[0].bank_id = 0xDEADBEEFU;
    publication.mappings[0].generation = generation - 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, false, true));
    assert_invalid(&publication, generation - 1U, 0xDEADBEEFU);

    memset(&publication, 0, sizeof(publication));
    publication.committed = 1U;
    publication.generation = generation - 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, false));
    assert_invalid(&publication, generation - 1U, 0xDEADBEEFU);

    memset(&publication, 0, sizeof(publication));
    publication.committed = 2U;
    publication.generation = generation - 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert_invalid(&publication, generation - 1U, bank_id);

    memset(&publication, 0, sizeof(publication));
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, 0U, true, true));
    assert_invalid(&publication, generation, bank_id);
    assert(sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation + 1U, true,
        true));
    assert(publication.committed == 1U &&
           publication.generation == generation + 1U);

    memset(&publication, 0, sizeof(publication));
    publication.committed = 1U;
    publication.generation = generation + 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert_invalid(&publication, generation + 1U, bank_id);

    memset(&publication, 0, sizeof(publication));
    publication.committed = 1U;
    publication.generation = generation - 1U;
    reset_dma(&publication, 0U, 0U);
    assert(sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert_invalid(&publication, generation, bank_id);
}

static void test_partitions_and_dma_fail_closed(const fixture_t *fixture)
{
    aligned_vram_t vram;
    sm64_saturn_actor_texture_publication_t publication = {0};
    vdp1_vram_partitions_t partitions;
    const uint32_t generation = fixture->view.package_generation + 101U;
    const uint32_t bank_id = be32(fixture->view.bytes +
        fixture->view.variant_records_offset + 56U);

    partitions = partitions_for(&vram, 15U, sizeof(vram.clut));
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);
    assert_invalid(&publication, generation, bank_id);

    partitions = partitions_for(&vram, sizeof(vram.texture), 31U);
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);
    assert_invalid(&publication, generation, bank_id);

    partitions = partitions_for(&vram, 16U, 32U);
    reset_dma(&publication, 0U, 0U);
    assert(sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));

    memset(&publication, 0, sizeof(publication));
    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.texture_base = vram.texture + 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.clut_base = vram.clut + 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.clut_base = vram.texture;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.texture_base = NULL;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.clut_base = NULL;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.texture_base = (void *)(UINTPTR_MAX - 7U);
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    partitions.clut_base = (void *)(UINTPTR_MAX - 15U);
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    partitions = partitions_for(&vram, sizeof(vram.texture), sizeof(vram.clut));
    reset_dma(&publication, 1U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 1U && g_wait_count == 0U);
    assert_invalid(&publication, generation, bank_id);

    reset_dma(&publication, 0U, 1U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 1U && g_wait_count == 1U);
    assert_invalid(&publication, generation, bank_id);
}

static void test_address_boundaries(void)
{
    sm64_saturn_texture_residency_t texture_region = {
        (uint8_t *)(UINTPTR_MAX - 15U), 16U, 0U, 0U, false
    };
    sm64_saturn_texture_residency_t empty_region = {
        NULL, 0U, 0U, 0U, false
    };
    uintptr_t address;

    assert(address_span_last(UINTPTR_MAX - 15U, 16U, &address));
    assert(address == UINTPTR_MAX);
    assert(!address_span_last(UINTPTR_MAX - 15U, 17U, &address));
    assert(checked_span_address((const void *)(UINTPTR_MAX - 15U), 0U,
                                16U, &address));
    assert(address == UINTPTR_MAX - 15U);
    assert(!checked_span_address((const void *)(UINTPTR_MAX - 15U), 8U,
                                 9U, &address));
    assert(regions_fit(&texture_region, &empty_region, 16U, 0U));
    assert(!ranges_overlap(0x1000U, 0x1007U, 0x1008U, 0x101FU));
    assert(ranges_overlap(0x1000U, 0x1008U, 0x1008U, 0x101FU));
}

static void rehash_bundle(uint8_t *bytes, uint32_t size)
{
    uint8_t digest[32];
    memset(bytes + 64U, 0, 32U);
    assert(sm64_saturn_sha256_digest(bytes, size, digest));
    memcpy(bytes + 64U, digest, 32U);
}

static void test_bundle_revalidation_and_collision(fixture_t *fixture)
{
    aligned_vram_t vram;
    sm64_saturn_actor_texture_publication_t publication = {0};
    vdp1_vram_partitions_t partitions = partitions_for(
        &vram, sizeof(vram.texture), sizeof(vram.clut));
    sm64_saturn_actor_bundle_view_t mismatched = fixture->view;
    const uint32_t generation = fixture->view.package_generation + 101U;
    uint8_t saved;

    mismatched.bank_payloads_size--;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &mismatched, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    mismatched = fixture->view;
    mismatched.variant_count--;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &mismatched, &partitions, generation, true, true));
    assert(g_submit_count == 0U);

    saved = fixture->bytes[fixture->size - 1U];
    fixture->bytes[fixture->size - 1U] ^= 1U;
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, &partitions, generation, true, true));
    assert(g_submit_count == 0U);
    fixture->bytes[fixture->size - 1U] = saved;

    /* Re-sign a second bank after forcing only its 32-bit scalar ID to alias
     * the first bank. Full hashes remain distinct; the canonical S64F parser
     * must reject the scalar collision rather than deduplicating it. */
    {
        const uint32_t variants = fixture->view.variant_records_offset;
        uint8_t *second = fixture->bytes + variants +
            SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
        uint32_t bank_offset = be32(second + 8U);
        uint32_t bank_size = be32(second + 12U);
        uint8_t *bank = fixture->bytes + fixture->view.bank_payloads_offset +
            bank_offset;
        uint32_t old_bank_id = be32(bank + 26U);
        uint8_t digest[32];
        uint32_t first_bank_id = be32(fixture->bytes + variants + 56U);
        put32(bank + 26U, first_bank_id);
        put32(second + 56U, first_bank_id);
        assert(sm64_saturn_sha256_digest(bank, bank_size, digest));
        memcpy(second + 24U, digest, 32U);
        rehash_bundle(fixture->bytes, fixture->size);
        reset_dma(&publication, 0U, 0U);
        assert(!sm64_saturn_actor_texture_residency_activate(
            &publication, &fixture->view, &partitions, generation, true, true));
        assert(g_submit_count == 0U);
        put32(bank + 26U, old_bank_id);
        put32(second + 56U, old_bank_id);
        assert(sm64_saturn_sha256_digest(bank, bank_size, digest));
        memcpy(second + 24U, digest, 32U);
        rehash_bundle(fixture->bytes, fixture->size);
        assert(sm64_saturn_actor_bundle_validate(
            fixture->bytes, fixture->size, &fixture->view));
    }
}

static void test_nulls(const fixture_t *fixture)
{
    aligned_vram_t vram;
    sm64_saturn_actor_texture_publication_t publication = {0};
    sm64_saturn_actor_texture_mapping_t mapping;
    vdp1_vram_partitions_t partitions = partitions_for(
        &vram, sizeof(vram.texture), sizeof(vram.clut));
    reset_dma(&publication, 0U, 0U);
    assert(!sm64_saturn_actor_texture_residency_activate(
        NULL, &fixture->view, &partitions, fixture->view.package_generation,
        true, true));
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, NULL, &partitions, fixture->view.package_generation,
        true, true));
    assert(!sm64_saturn_actor_texture_residency_activate(
        &publication, &fixture->view, NULL, fixture->view.package_generation,
        true, true));
    assert(!sm64_saturn_actor_texture_residency_lookup(
        NULL, fixture->view.package_generation, 1U, &mapping));
    assert(!sm64_saturn_actor_texture_residency_lookup(
        &publication, fixture->view.package_generation, 1U, NULL));
}

int main(int argc, char **argv)
{
    fixture_t mixed, real;
    assert(argc == 3);
    mixed = load_fixture(argv[1]);
    real = load_fixture(argv[2]);
    assert(mixed.view.variant_count == 2U);
    assert(real.view.family_count == 47U && real.view.variant_count == 14U);
    test_mixed_and_real_success(&mixed, &real);
    test_lifecycle_and_prior_state_fail_closed(&mixed);
    test_partitions_and_dma_fail_closed(&mixed);
    test_address_boundaries();
    test_bundle_revalidation_and_collision(&mixed);
    test_nulls(&mixed);
    free(real.bytes);
    free(mixed.bytes);
    puts("actor texture residency: PASS");
    return 0;
}
