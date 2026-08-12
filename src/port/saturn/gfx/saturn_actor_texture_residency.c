#include "saturn_actor_texture_residency.h"

#include "saturn_texture_residency.h"
#include "../gpl/slavedriver_dma_queue.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void publication_fence(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#endif
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || b > UINT32_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool align_u32(uint32_t value, uint32_t alignment, uint32_t *out)
{
    if (out == NULL || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U ||
        value > UINT32_MAX - (alignment - 1U))
        return false;
    *out = (value + alignment - 1U) & ~(alignment - 1U);
    return true;
}

/* RFC-1982-style serial ordering over the nonzero 32-bit generation space.
 * The exact half-range is intentionally unordered, avoiding any dependency on
 * implementation-defined unsigned-to-signed conversion. */
static bool generation_is_newer(uint32_t candidate, uint32_t current)
{
    const uint32_t distance = candidate - current;
    return candidate != 0U && current != 0U && distance != 0U &&
           distance < UINT32_C(0x80000000);
}

static bool address_span_last(uintptr_t base, uint32_t bytes,
                              uintptr_t *last)
{
    if (last == NULL) return false;
    if (bytes == 0U) {
        *last = base;
        return true;
    }
    if ((uintptr_t)(bytes - 1U) > UINTPTR_MAX - base) return false;
    *last = base + (uintptr_t)(bytes - 1U);
    return true;
}

#define SH2_PHYSICAL_ADDRESS_MASK UINT32_C(0x1FFFFFFF)
#define SH2_AREA_MASK UINT32_C(0xE0000000)
#define SH2_AREA_CACHED UINT32_C(0x00000000)
#define SH2_AREA_CACHE_THROUGH UINT32_C(0x20000000)
#define SH2_HWRAM_PHYSICAL_START UINT32_C(0x06000000)
#define SH2_HWRAM_PHYSICAL_END UINT32_C(0x06100000)

static bool normalized_span(const void *base, uint32_t bytes,
                            uintptr_t *start, uintptr_t *last)
{
    uintptr_t address;
    if (base == NULL || bytes == 0U || start == NULL || last == NULL)
        return false;
    address = (uintptr_t)base;
#if defined(SM64_SATURN_ACTOR_TEXTURE_RESIDENCY_HOST_TEST)
    /* Native fixtures live outside the 32-bit SH-2 address space. Keep their
     * real addresses so overlap tests retain ordinary host pointer semantics. */
    if (address > UINT32_MAX) {
        *start = address;
        return address_span_last(address, bytes, last);
    }
#else
    if (address > UINT32_MAX) return false;
#endif
    address &= (uintptr_t)SH2_PHYSICAL_ADDRESS_MASK;
    *start = address;
    return address_span_last(address, bytes, last);
}

static bool stage_span_is_hwram(const void *staging,
                                uint32_t staging_capacity)
{
    uintptr_t address = (uintptr_t)staging;
    uint32_t area;
    uint32_t physical;
#if defined(SM64_SATURN_ACTOR_TEXTURE_RESIDENCY_HOST_TEST)
    uintptr_t ignored_last;
    if (address > UINT32_MAX)
        return address_span_last(address, staging_capacity, &ignored_last) &&
               staging_capacity != 0U;
#else
    if (address > UINT32_MAX) return false;
#endif
    if (staging == NULL || staging_capacity == 0U) return false;
    /* Yaul's SH-2 cache ABI exposes HWRAM through the ordinary cached P0
     * shape and the P2 cache-through shape only. P1/P3/P4-shaped aliases are
     * cache-control/address/data arrays, not CPU-copyable memory, even when
     * masking their high bits happens to yield an HWRAM physical address. */
    area = (uint32_t)address & SH2_AREA_MASK;
    if (area != SH2_AREA_CACHED && area != SH2_AREA_CACHE_THROUGH)
        return false;
    physical = (uint32_t)address & SH2_PHYSICAL_ADDRESS_MASK;
    return physical >= SH2_HWRAM_PHYSICAL_START &&
           physical < SH2_HWRAM_PHYSICAL_END &&
           staging_capacity <= SH2_HWRAM_PHYSICAL_END - physical;
}

static bool checked_span_address(const void *base, uint32_t offset,
                                 uint32_t bytes, uintptr_t *start)
{
    uintptr_t address, ignored_last;
    if (start == NULL) return false;
    if (base == NULL) {
        if (offset != 0U || bytes != 0U) return false;
        *start = 0U;
        return true;
    }
    if ((uintptr_t)offset > UINTPTR_MAX - (uintptr_t)base)
        return false;
    address = (uintptr_t)base + (uintptr_t)offset;
    if (!address_span_last(address, bytes, &ignored_last)) return false;
    *start = address;
    return true;
}

static bool ranges_overlap(uintptr_t a, uintptr_t a_last,
                           uintptr_t b, uintptr_t b_last)
{
    return a <= b_last && b <= a_last;
}

static bool memory_spans_overlap(const void *a, uint32_t a_bytes,
                                 const void *b, uint32_t b_bytes)
{
    uintptr_t a_start, a_last, b_start, b_last;
    if (a_bytes == 0U || b_bytes == 0U) return false;
    if (!normalized_span(a, a_bytes, &a_start, &a_last) ||
        !normalized_span(b, b_bytes, &b_start, &b_last))
        return true;
    return ranges_overlap(a_start, a_last, b_start, b_last);
}

static void invalidate_publication(
    sm64_saturn_actor_texture_publication_t *publication)
{
    if (publication == NULL) return;
    publication->committed = 0U;
    publication_fence();
    memset(publication->mappings, 0, sizeof(publication->mappings));
    publication->texture_bytes = 0U;
    publication->clut_bytes = 0U;
    publication->generation = 0U;
    publication->mapping_count = 0U;
    publication->reserved = 0U;
    publication_fence();
}

static bool mapping_is_zero(
    const sm64_saturn_actor_texture_mapping_t *mapping)
{
    const sm64_saturn_actor_texture_mapping_t zero = {0};
    return memcmp(mapping, &zero, sizeof(zero)) == 0;
}

static bool publication_entries_valid(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation)
{
    uint16_t index, prior;
    uint32_t prior_texture = 0U;
    uint16_t prior_clut = 0U;
    if (publication == NULL || generation == 0U ||
        publication->mapping_count > SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ||
        (publication->texture_bytes & 7U) != 0U ||
        publication->texture_bytes > ((uint32_t)UINT16_MAX + 1U) * 8U ||
        (publication->clut_bytes & 31U) != 0U ||
        publication->clut_bytes > ((uint32_t)UINT16_MAX + 1U) * 32U)
        return false;
    if (publication->mapping_count == 0U &&
        (publication->texture_bytes != 0U || publication->clut_bytes != 0U))
        return false;
    for (index = 0U; index < publication->mapping_count; index++) {
        const sm64_saturn_actor_texture_mapping_t *mapping =
            &publication->mappings[index];
        if (mapping->bank_id == 0U || mapping->tile_count == 0U ||
            mapping->generation != generation ||
            (mapping->texture_base_offset & 7U) != 0U ||
            mapping->texture_base_offset >= publication->texture_bytes ||
            (uint32_t)mapping->clut_base_index * 32U >=
                publication->clut_bytes ||
            (index != 0U &&
             (mapping->texture_base_offset < prior_texture ||
              mapping->clut_base_index < prior_clut)))
            return false;
        for (prior = 0U; prior < index; prior++)
            if (publication->mappings[prior].bank_id == mapping->bank_id)
                return false;
        prior_texture = mapping->texture_base_offset;
        prior_clut = mapping->clut_base_index;
    }
    for (; index < SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY; index++)
        if (!mapping_is_zero(&publication->mappings[index])) return false;
    return true;
}

bool sm64_saturn_actor_texture_publication_validate(
    const sm64_saturn_actor_texture_publication_t *publication)
{
    uint16_t index;
    if (publication == NULL || publication->committed > 1U ||
        publication->reserved != 0U || publication->mapping_count >
            SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY)
        return false;
    if (publication->committed == 0U) {
        if (publication->generation != 0U ||
            publication->mapping_count != 0U ||
            publication->texture_bytes != 0U ||
            publication->clut_bytes != 0U)
            return false;
        for (index = 0U;
             index < SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY; index++)
            if (!mapping_is_zero(&publication->mappings[index])) return false;
        return true;
    }
    return publication_entries_valid(publication, publication->generation);
}

static bool current_publication_valid(
    const sm64_saturn_actor_texture_publication_t *publication)
{
    if (publication == NULL) return false;
    /* Activation may begin from the exact empty state or a complete committed
     * generation. Partial/uncommitted data is never reusable. */
    return sm64_saturn_actor_texture_publication_validate(publication);
}

static bool bundle_views_equal(const sm64_saturn_actor_bundle_view_t *a,
                               const sm64_saturn_actor_bundle_view_t *b)
{
    uint16_t word;
    if (a == NULL || b == NULL || a->bytes != b->bytes ||
        a->byte_count != b->byte_count ||
        a->family_records_offset != b->family_records_offset ||
        a->variant_records_offset != b->variant_records_offset ||
        a->metadata_offset != b->metadata_offset ||
        a->metadata_size != b->metadata_size ||
        a->bank_payloads_offset != b->bank_payloads_offset ||
        a->bank_payloads_size != b->bank_payloads_size ||
        a->workspace_lane_stride != b->workspace_lane_stride ||
        a->maximum_scratch != b->maximum_scratch ||
        a->package_generation != b->package_generation ||
        a->family_count != b->family_count ||
        a->variant_count != b->variant_count)
        return false;
    for (word = 0U; word < 8U; word++)
        if (a->content_hash_words[word] != b->content_hash_words[word])
            return false;
    return true;
}

static bool resolve_variant_index(
    const sm64_saturn_actor_bundle_view_t *bundle, uint16_t index,
    sm64_saturn_actor_bundle_variant_t *variant,
    sm64_saturn_actor_bank_view_t *bank)
{
    const uint8_t *record;
    uint32_t source_hash[8];
    uint16_t word;
    if (bundle == NULL || variant == NULL || bank == NULL ||
        index >= bundle->variant_count)
        return false;
    record = bundle->bytes + bundle->variant_records_offset +
        (uint32_t)index * SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
    for (word = 0U; word < 8U; word++)
        source_hash[word] = read_be32(record + 56U + (uint32_t)word * 4U);
    if (!sm64_saturn_actor_bundle_variant(
            bundle, read_be16(record), read_be16(record + 2U), variant))
        return false;
    return sm64_saturn_actor_bundle_resolve(
        bundle, variant->family_ordinal, variant->model_id, source_hash[0],
        source_hash, bank);
}

static bool plan_bundle(const sm64_saturn_actor_bundle_view_t *bundle,
                        uint32_t *texture_bytes, uint32_t *clut_bytes,
                        uint16_t *mapping_count)
{
    uint32_t texture_cursor = 0U, clut_cursor = 0U;
    uint16_t count = 0U, index;
    if (bundle == NULL || texture_bytes == NULL || clut_bytes == NULL ||
        mapping_count == NULL)
        return false;
    for (index = 0U; index < bundle->variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        uint32_t aligned;
        uintptr_t source;
        if (!resolve_variant_index(bundle, index, &variant, &bank)) return false;
        (void)variant;
        if (bank.bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2) continue;
        if (count >= SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ||
            !align_u32(texture_cursor, 8U, &aligned) ||
            aligned / 8U > UINT16_MAX)
            return false;
        texture_cursor = aligned;
        if (!add_u32(texture_cursor, bank.texture_resident_bytes,
                     &texture_cursor) ||
            texture_cursor > ((uint32_t)UINT16_MAX + 1U) * 8U ||
            !align_u32(clut_cursor, 32U, &aligned) ||
            aligned / 32U > UINT16_MAX)
            return false;
        clut_cursor = aligned;
        if (!add_u32(clut_cursor, bank.clut_resident_bytes, &clut_cursor) ||
            clut_cursor / 32U > (uint32_t)UINT16_MAX + 1U ||
            !checked_span_address(bank.bytes, bank.texture_payload_offset,
                                  bank.texture_resident_bytes, &source) ||
            !checked_span_address(bank.bytes, bank.clut_payload_offset,
                                  bank.clut_resident_bytes, &source))
            return false;
        count++;
    }
    *texture_bytes = texture_cursor;
    *clut_bytes = clut_cursor;
    *mapping_count = count;
    return true;
}

bool sm64_saturn_actor_texture_residency_requirements(
    const sm64_saturn_actor_bundle_view_t *bundle,
    uint32_t *texture_bytes, uint32_t *clut_bytes,
    uint16_t *mapping_count)
{
    sm64_saturn_actor_bundle_view_t validated;
    if (texture_bytes != NULL) *texture_bytes = 0U;
    if (clut_bytes != NULL) *clut_bytes = 0U;
    if (mapping_count != NULL) *mapping_count = 0U;
    return bundle != NULL && bundle->bytes != NULL &&
        texture_bytes != NULL && clut_bytes != NULL &&
        mapping_count != NULL &&
        sm64_saturn_actor_bundle_validate(bundle->bytes, bundle->byte_count,
                                           &validated) &&
        bundle_views_equal(bundle, &validated) &&
        plan_bundle(&validated, texture_bytes, clut_bytes, mapping_count);
}

static bool regions_fit(const sm64_saturn_texture_residency_t *texture_region,
                        const sm64_saturn_texture_residency_t *clut_region,
                        uint32_t texture_bytes, uint32_t clut_bytes)
{
    uintptr_t texture = texture_region == NULL ? 0U :
        (uintptr_t)texture_region->base;
    uintptr_t clut = clut_region == NULL ? 0U :
        (uintptr_t)clut_region->base;
    uintptr_t texture_last, clut_last;
    if (texture_region == NULL || clut_region == NULL ||
        (texture_bytes != 0U &&
         (texture_region->base == NULL || (texture & 7U) != 0U)) ||
        (clut_bytes != 0U &&
         (clut_region->base == NULL || (clut & 31U) != 0U)) ||
        texture_bytes > texture_region->capacity ||
        clut_bytes > clut_region->capacity ||
        !address_span_last(texture, texture_bytes, &texture_last) ||
        !address_span_last(clut, clut_bytes, &clut_last))
        return false;
    return texture_bytes == 0U || clut_bytes == 0U ||
           !ranges_overlap(texture, texture_last, clut, clut_last);
}

static bool transfer_span(void *destination, const void *source,
                          void *staging, uint32_t bytes)
{
    saturn_dma_queue_sequence_t sequence;
    if (bytes == 0U) return true;
    memcpy(staging, source, bytes);
    sequence = saturn_dma_queue_submit(destination, staging, bytes,
                                       SATURN_DMA_QUEUE_SCU);
    return sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID &&
           saturn_dma_queue_wait(sequence) != 0;
}

static bool preflight_span(const sm64_saturn_texture_residency_t *region,
                           uint32_t offset, uint32_t bytes,
                           void *staging, uint32_t staging_capacity)
{
    uintptr_t destination, stage_source;
    if (bytes == 0U) return true;
    return bytes <= staging_capacity &&
           checked_span_address(region->base, offset, bytes, &destination) &&
           checked_span_address(staging, 0U, bytes, &stage_source) &&
           saturn_dma_queue_request_valid(
               (void *)destination, (const void *)stage_source, bytes,
               SATURN_DMA_QUEUE_SCU) != 0;
}

static bool preflight_bundle(
    const sm64_saturn_actor_bundle_view_t *bundle,
    const sm64_saturn_texture_residency_t *texture_region,
    const sm64_saturn_texture_residency_t *clut_region,
    void *staging, uint32_t staging_capacity)
{
    uint32_t texture_cursor = 0U, clut_cursor = 0U;
    uint16_t index;
    if (staging == NULL || ((uintptr_t)staging & 3U) != 0U) return false;
    for (index = 0U; index < bundle->variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        if (!resolve_variant_index(bundle, index, &variant, &bank)) return false;
        (void)variant;
        if (bank.bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2) continue;
        if (!align_u32(texture_cursor, 8U, &texture_cursor) ||
            !align_u32(clut_cursor, 32U, &clut_cursor) ||
            !preflight_span(texture_region, texture_cursor,
                            bank.texture_resident_bytes,
                            staging, staging_capacity) ||
            !preflight_span(clut_region, clut_cursor,
                            bank.clut_resident_bytes,
                            staging, staging_capacity) ||
            !add_u32(texture_cursor, bank.texture_resident_bytes,
                     &texture_cursor) ||
            !add_u32(clut_cursor, bank.clut_resident_bytes, &clut_cursor))
            return false;
    }
    return true;
}

static bool upload_bundle(const sm64_saturn_actor_bundle_view_t *bundle,
                          const sm64_saturn_texture_residency_t *texture_region,
                          const sm64_saturn_texture_residency_t *clut_region,
                          void *staging)
{
    uint32_t texture_cursor = 0U, clut_cursor = 0U;
    uint16_t index;
    for (index = 0U; index < bundle->variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        uintptr_t texture_destination, clut_destination;
        uintptr_t texture_source, clut_source;
        if (!resolve_variant_index(bundle, index, &variant, &bank)) return false;
        (void)variant;
        if (bank.bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2) continue;
        if (!align_u32(texture_cursor, 8U, &texture_cursor) ||
            !align_u32(clut_cursor, 32U, &clut_cursor) ||
            !checked_span_address(texture_region->base, texture_cursor,
                                  bank.texture_resident_bytes,
                                  &texture_destination) ||
            !checked_span_address(clut_region->base, clut_cursor,
                                  bank.clut_resident_bytes,
                                  &clut_destination) ||
            !checked_span_address(bank.bytes, bank.texture_payload_offset,
                                  bank.texture_resident_bytes,
                                  &texture_source) ||
            !checked_span_address(bank.bytes, bank.clut_payload_offset,
                                  bank.clut_resident_bytes, &clut_source) ||
            !transfer_span((void *)texture_destination,
                           (const void *)texture_source,
                           staging,
                           bank.texture_resident_bytes) ||
            !transfer_span((void *)clut_destination,
                           (const void *)clut_source,
                           staging,
                           bank.clut_resident_bytes) ||
            !add_u32(texture_cursor, bank.texture_resident_bytes,
                     &texture_cursor) ||
            !add_u32(clut_cursor, bank.clut_resident_bytes, &clut_cursor))
            return false;
    }
    return true;
}

static bool build_mappings(sm64_saturn_actor_texture_publication_t *publication,
                           const sm64_saturn_actor_bundle_view_t *bundle,
                           uint32_t generation)
{
    uint32_t texture_cursor = 0U, clut_cursor = 0U;
    uint16_t count = 0U, index;
    for (index = 0U; index < bundle->variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        sm64_saturn_actor_texture_mapping_t *mapping;
        if (!resolve_variant_index(bundle, index, &variant, &bank)) return false;
        (void)variant;
        if (bank.bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2) continue;
        if (count >= SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ||
            !align_u32(texture_cursor, 8U, &texture_cursor) ||
            !align_u32(clut_cursor, 32U, &clut_cursor) ||
            clut_cursor / 32U > UINT16_MAX)
            return false;
        mapping = &publication->mappings[count++];
        mapping->bank_id = bank.bank.source_hash_words[0];
        mapping->texture_base_offset = texture_cursor;
        mapping->clut_base_index = (uint16_t)(clut_cursor / 32U);
        mapping->tile_count = bank.tile_count;
        mapping->generation = generation;
        if (!add_u32(texture_cursor, bank.texture_resident_bytes,
                     &texture_cursor) ||
            !add_u32(clut_cursor, bank.clut_resident_bytes, &clut_cursor))
            return false;
    }
    publication->texture_bytes = texture_cursor;
    publication->clut_bytes = clut_cursor;
    publication->mapping_count = count;
    return true;
}

bool sm64_saturn_actor_texture_residency_activate(
    sm64_saturn_actor_texture_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    const vdp1_vram_partitions_t *partitions,
    void *staging, uint32_t staging_capacity,
    uint32_t generation, bool gameplay_suspended, bool vdp1_idle)
{
    sm64_saturn_actor_bundle_view_t validated;
    sm64_saturn_texture_residency_t texture_region, clut_region;
    uint32_t texture_bytes, clut_bytes, prior_generation = 0U;
    uint16_t mapping_count;
    bool prior_committed, prior_valid;
    if (publication == NULL) return false;
    prior_committed = publication->committed == 1U;
    prior_generation = publication->generation;
    prior_valid = current_publication_valid(publication);
    invalidate_publication(publication);
    if (partitions != NULL) {
        sm64_saturn_texture_residency_init_region(
            &texture_region, partitions->texture_base, partitions->texture_size);
        sm64_saturn_texture_residency_init_region(
            &clut_region, partitions->clut_base, partitions->clut_size);
    } else {
        sm64_saturn_texture_residency_init_region(&texture_region, NULL, 0U);
        sm64_saturn_texture_residency_init_region(&clut_region, NULL, 0U);
    }
    if (!prior_valid || bundle == NULL || partitions == NULL ||
        !gameplay_suspended || !vdp1_idle || generation == 0U ||
        (prior_committed &&
         !generation_is_newer(generation, prior_generation)) ||
        !sm64_saturn_actor_bundle_validate(bundle->bytes, bundle->byte_count,
                                            &validated) ||
        !bundle_views_equal(bundle, &validated) ||
        !stage_span_is_hwram(staging, staging_capacity) ||
        memory_spans_overlap(staging, staging_capacity,
                             publication, (uint32_t)sizeof(*publication)) ||
        memory_spans_overlap(staging, staging_capacity,
                             validated.bytes, validated.byte_count) ||
        memory_spans_overlap(staging, staging_capacity,
                             texture_region.base, texture_region.capacity) ||
        memory_spans_overlap(staging, staging_capacity,
                             clut_region.base, clut_region.capacity) ||
        !plan_bundle(&validated, &texture_bytes, &clut_bytes, &mapping_count) ||
        mapping_count > SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ||
        !regions_fit(&texture_region, &clut_region,
                     texture_bytes, clut_bytes) ||
        !preflight_bundle(&validated, &texture_region, &clut_region,
                          staging, staging_capacity) ||
        !upload_bundle(&validated, &texture_region, &clut_region, staging) ||
        !build_mappings(publication, &validated, generation) ||
        publication->texture_bytes != texture_bytes ||
        publication->clut_bytes != clut_bytes ||
        publication->mapping_count != mapping_count ||
        !publication_entries_valid(publication, generation)) {
        invalidate_publication(publication);
        return false;
    }
    publication_fence();
    publication->generation = generation;
    publication_fence();
    publication->committed = 1U;
    return true;
}

bool sm64_saturn_actor_texture_residency_lookup(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation, uint32_t bank_id,
    sm64_saturn_actor_texture_mapping_t *out)
{
    uint16_t index;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (publication == NULL || out == NULL || generation == 0U ||
        bank_id == 0U ||
        !sm64_saturn_actor_texture_publication_validate(publication) ||
        publication->committed != 1U)
        return false;
    publication_fence();
    if (publication->generation != generation ||
        publication->mapping_count > SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY)
        return false;
    for (index = 0U; index < publication->mapping_count; index++) {
        sm64_saturn_actor_texture_mapping_t found = publication->mappings[index];
        if (found.bank_id != bank_id) continue;
        if (found.generation != generation) return false;
        publication_fence();
        if (publication->committed != 1U ||
            publication->generation != generation)
            return false;
        *out = found;
        return true;
    }
    return false;
}
