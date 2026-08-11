#include "saturn_actor_bundle.h"
#include "../runtime/saturn_sha256.h"

#include <limits.h>
#include <string.h>

#define FAMILY_FLAG_SUPPORTED (1UL << 0)
#define FAMILY_FLAG_GEOMETRY (1UL << 1)
#define FAMILY_FLAGS (FAMILY_FLAG_SUPPORTED | FAMILY_FLAG_GEOMETRY)
#define FAMILY_CAPABILITY_MASK ((1UL << 18) - 1UL)
#define FAMILY_RUNTIME_CAPABILITY_MASK ((1UL << 5) - 1UL)

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || b > UINT32_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool multiply_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || (a != 0U && b > UINT32_MAX / a)) return false;
    *out = a * b;
    return true;
}

static bool align4_u32(uint32_t value, uint32_t *out)
{
    uint32_t biased;
    if (!add_u32(value, 3U, &biased)) return false;
    *out = biased & ~3U;
    return true;
}

static bool span_u32(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

static bool zero_span(const uint8_t *bytes, uint32_t begin, uint32_t end)
{
    uint32_t index;
    if (bytes == NULL || begin > end) return false;
    for (index = begin; index < end; index++)
        if (bytes[index] != 0U) return false;
    return true;
}

static bool bundle_digest(const uint8_t *bytes, uint32_t byte_count,
                          uint8_t digest[32])
{
    sm64_saturn_sha256_t sha;
    static const uint8_t zero[32] = {0};
    if (bytes == NULL || digest == NULL || byte_count < 96U) return false;
    sm64_saturn_sha256_init(&sha);
    return sm64_saturn_sha256_update(&sha, bytes, 64U) &&
           sm64_saturn_sha256_update(&sha, zero, sizeof(zero)) &&
           sm64_saturn_sha256_update(&sha, bytes + 96U, byte_count - 96U) &&
           sm64_saturn_sha256_finish(&sha, digest);
}

static bool metadata_span_is_canonical(const uint8_t *bytes,
                                       uint32_t metadata_offset,
                                       uint32_t metadata_size,
                                       uint32_t family_offset,
                                       uint16_t family_index,
                                       uint16_t span_index,
                                       uint32_t offset, uint32_t size,
                                       uint32_t *cursor)
{
    uint16_t prior_family, prior_span;
    if (cursor == NULL || !span_u32(offset, size, metadata_size)) return false;
    if (size == 0U) return offset == 0U;
    for (prior_family = 0U; prior_family <= family_index; prior_family++) {
        uint16_t stop = prior_family == family_index ? span_index : 4U;
        const uint8_t *record = bytes + family_offset +
            (uint32_t)prior_family * SM64_SATURN_ACTOR_BUNDLE_FAMILY_RECORD_SIZE;
        for (prior_span = 0U; prior_span < stop; prior_span++) {
            uint32_t other_offset = read_be32(record + 24U + (uint32_t)prior_span * 8U);
            uint32_t other_size = read_be32(record + 28U + (uint32_t)prior_span * 8U);
            if (other_size == size &&
                memcmp(bytes + metadata_offset + other_offset,
                       bytes + metadata_offset + offset, size) == 0)
                return offset == other_offset;
        }
    }
    if (offset != *cursor || !add_u32(*cursor, size, cursor)) return false;
    return true;
}

static bool hash_equals_bytes(const uint32_t words[8], const uint8_t bytes[32])
{
    uint16_t index;
    if (words == NULL || bytes == NULL) return false;
    for (index = 0U; index < 8U; index++)
        if (words[index] != read_be32(bytes + (uint32_t)index * 4U)) return false;
    return true;
}

static bool read_variant_record(const sm64_saturn_actor_bundle_view_t *view,
                                uint16_t index,
                                sm64_saturn_actor_bundle_variant_t *out)
{
    const uint8_t *record;
    uint16_t word;
    uint32_t table_size;
    if (view == NULL || out == NULL || view->bytes == NULL ||
        index >= view->variant_count ||
        !multiply_u32(view->variant_count,
                      SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE,
                      &table_size) ||
        !span_u32(view->variant_records_offset, table_size, view->byte_count))
        return false;
    record = view->bytes + view->variant_records_offset +
        (uint32_t)index * SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
    if (read_be32(record + 4U) != 0U) return false;
    out->family_ordinal = read_be16(record);
    out->model_id = read_be16(record + 2U);
    out->bank_offset = read_be32(record + 8U);
    out->bank_size = read_be32(record + 12U);
    out->bank_lane_bytes = read_be32(record + 16U);
    out->bank_maximum_scratch = read_be32(record + 20U);
    for (word = 0U; word < 8U; word++) {
        out->payload_hash_words[word] = read_be32(record + 24U + (uint32_t)word * 4U);
        out->source_hash_words[word] = read_be32(record + 56U + (uint32_t)word * 4U);
    }
    if (out->family_ordinal == 0U || out->model_id == 0U ||
        (out->bank_offset & 3U) != 0U || out->bank_size == 0U ||
        !span_u32(view->bank_payloads_offset, view->bank_payloads_size,
                  view->byte_count) ||
        !span_u32(out->bank_offset, out->bank_size, view->bank_payloads_size) ||
        out->bank_lane_bytes == 0U || (out->bank_lane_bytes & 3U) != 0U ||
        out->bank_lane_bytes > (UINT32_MAX - 3U) / 2U ||
        out->bank_maximum_scratch != 3U + 2U * out->bank_lane_bytes)
        return false;
    return true;
}

bool sm64_saturn_actor_bundle_validate(const void *data, uint32_t byte_count,
                                       sm64_saturn_actor_bundle_view_t *out)
{
    const uint8_t *bytes = (const uint8_t *)data;
    sm64_saturn_actor_bundle_view_t parsed;
    uint16_t family_count, variant_count, index, prior;
    uint32_t family_table_size, variant_table_size, expected_variant;
    uint32_t expected_metadata, metadata_end, expected_bank, bank_end;
    uint32_t metadata_cursor = 0U, variant_cursor = 0U, bank_cursor = 0U;
    uint32_t maximum_lane = 0U;
    uint8_t digest[32];
    bool digest_nonzero = false;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (bytes == NULL || out == NULL || byte_count < 96U) return false;
    memset(&parsed, 0, sizeof(parsed));
    if (read_be32(bytes) != SM64_SATURN_ACTOR_BUNDLE_MAGIC ||
        read_be16(bytes + 4U) != SM64_SATURN_ACTOR_BUNDLE_VERSION ||
        read_be16(bytes + 6U) != SM64_SATURN_ACTOR_BUNDLE_HEADER_SIZE ||
        read_be16(bytes + 12U) != SM64_SATURN_ACTOR_BUNDLE_FAMILY_RECORD_SIZE ||
        read_be16(bytes + 14U) != SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE)
        return false;
    family_count = read_be16(bytes + 8U);
    variant_count = read_be16(bytes + 10U);
    if (family_count == 0U || family_count > SM64_SATURN_ACTOR_BUNDLE_MAX_FAMILIES ||
        variant_count == 0U || variant_count > SM64_SATURN_ACTOR_BUNDLE_MAX_VARIANTS ||
        !multiply_u32(family_count, SM64_SATURN_ACTOR_BUNDLE_FAMILY_RECORD_SIZE,
                      &family_table_size) ||
        !multiply_u32(variant_count, SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE,
                      &variant_table_size) ||
        !add_u32(96U, family_table_size, &expected_variant) ||
        !add_u32(expected_variant, variant_table_size, &expected_metadata) ||
        !add_u32(expected_metadata, read_be32(bytes + 28U), &metadata_end) ||
        !align4_u32(metadata_end, &expected_bank) ||
        !add_u32(read_be32(bytes + 32U), read_be32(bytes + 36U), &bank_end))
        return false;
    if (read_be32(bytes + 16U) != 96U || read_be32(bytes + 20U) != expected_variant ||
        read_be32(bytes + 24U) != expected_metadata || read_be32(bytes + 32U) != expected_bank ||
        bank_end != byte_count || read_be32(bytes + 56U) != byte_count ||
        (byte_count & 3U) != 0U || read_be32(bytes + 48U) == 0U ||
        read_be32(bytes + 52U) != 0U || read_be32(bytes + 60U) != 0U ||
        !zero_span(bytes, metadata_end, expected_bank) || !bundle_digest(bytes, byte_count, digest))
        return false;
    for (index = 0U; index < 32U; index++) {
        digest_nonzero |= bytes[64U + index] != 0U;
        if (bytes[64U + index] != digest[index]) return false;
    }
    if (!digest_nonzero) return false;
    for (index = 0U; index < family_count; index++) {
        const uint8_t *record = bytes + 96U +
            (uint32_t)index * SM64_SATURN_ACTOR_BUNDLE_FAMILY_RECORD_SIZE;
        uint32_t stable = read_be32(record), flags = read_be32(record + 20U);
        uint16_t first = read_be16(record + 56U), count = read_be16(record + 58U);
        bool supported = (flags & FAMILY_FLAG_SUPPORTED) != 0U;
        if (stable == 0U || (index != 0U && stable <= read_be32(record - 64U)) ||
            (read_be32(record + 4U) & ~FAMILY_CAPABILITY_MASK) != 0U ||
            (read_be32(record + 8U) & ~FAMILY_RUNTIME_CAPABILITY_MASK) != 0U ||
            (flags & ~FAMILY_FLAGS) != 0U || read_be32(record + 60U) != 0U ||
            supported != (count != 0U) ||
            ((flags & FAMILY_FLAG_GEOMETRY) != 0U) != supported ||
            (supported && (read_be32(record + 12U) == 0U ||
                           read_be32(record + 16U) == 0U)) ||
            first != variant_cursor || count > (uint32_t)variant_count - first)
            return false;
        variant_cursor += count;
        for (prior = 0U; prior < 4U; prior++) {
            if (!metadata_span_is_canonical(bytes, expected_metadata,
                    read_be32(bytes + 28U), 96U, index, prior,
                    read_be32(record + 24U + (uint32_t)prior * 8U),
                    read_be32(record + 28U + (uint32_t)prior * 8U),
                    &metadata_cursor))
                return false;
        }
    }
    if (variant_cursor != variant_count || metadata_cursor != read_be32(bytes + 28U))
        return false;
    parsed.bytes = bytes;
    parsed.byte_count = byte_count;
    parsed.family_records_offset = 96U;
    parsed.variant_records_offset = expected_variant;
    parsed.metadata_offset = expected_metadata;
    parsed.metadata_size = read_be32(bytes + 28U);
    parsed.bank_payloads_offset = expected_bank;
    parsed.bank_payloads_size = read_be32(bytes + 36U);
    parsed.workspace_lane_stride = read_be32(bytes + 40U);
    parsed.maximum_scratch = read_be32(bytes + 44U);
    parsed.package_generation = read_be32(bytes + 48U);
    parsed.family_count = family_count;
    parsed.variant_count = variant_count;
    for (index = 0U; index < 8U; index++)
        parsed.content_hash_words[index] = read_be32(bytes + 64U + (uint32_t)index * 4U);
    for (index = 0U; index < variant_count; index++) {
        sm64_saturn_actor_bundle_variant_t variant;
        sm64_saturn_actor_bank_view_t bank;
        uint8_t payload_digest[32];
        uint32_t aligned_cursor, lane, usable, reserved_scratch;
        const uint8_t *family_record;
        if (!read_variant_record(&parsed, index, &variant) ||
            variant.family_ordinal == 0U || variant.family_ordinal > family_count ||
            variant.model_id == 0U ||
            !align4_u32(bank_cursor, &aligned_cursor) ||
            variant.bank_offset != aligned_cursor ||
            !zero_span(bytes, expected_bank + bank_cursor,
                       expected_bank + variant.bank_offset) ||
            !span_u32(variant.bank_offset, variant.bank_size, parsed.bank_payloads_size) ||
            variant.bank_size == 0U ||
            !sm64_saturn_sha256_digest(bytes + expected_bank + variant.bank_offset,
                                       variant.bank_size, payload_digest) ||
            !hash_equals_bytes(variant.payload_hash_words, payload_digest) ||
            !sm64_saturn_actor_bank_validate_expected(
                bytes + expected_bank + variant.bank_offset, variant.bank_size,
                variant.source_hash_words, &bank) ||
            bank.bank.family_id != variant.family_ordinal ||
            bank.bank.model_id != variant.model_id ||
            !sm64_saturn_actor_bank_workspace_requirements(
                bank.bank.vertex_count, bank.bank.joint_count, &lane, &usable,
                &reserved_scratch) || variant.bank_lane_bytes != lane ||
            variant.bank_maximum_scratch != reserved_scratch ||
            bank.max_scratch != reserved_scratch)
            return false;
        if (index != 0U) {
            const uint8_t *previous = bytes + expected_variant +
                (uint32_t)(index - 1U) * SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
            uint16_t previous_family = read_be16(previous);
            uint16_t previous_model = read_be16(previous + 2U);
            if (variant.family_ordinal < previous_family ||
                (variant.family_ordinal == previous_family && variant.model_id <= previous_model))
                return false;
        }
        family_record = bytes + 96U +
            (uint32_t)(variant.family_ordinal - 1U) * 64U;
        if (index < read_be16(family_record + 56U) ||
            index >= (uint32_t)read_be16(family_record + 56U) +
                     read_be16(family_record + 58U))
            return false;
        if (variant.source_hash_words[0] == 0U) return false;
        for (prior = 0U; prior < index; prior++) {
            const uint8_t *other = bytes + expected_variant +
                (uint32_t)prior * SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE;
            if (read_be32(other + 56U) == variant.source_hash_words[0]) return false;
        }
        bank_cursor = variant.bank_offset + variant.bank_size;
        if (lane > maximum_lane) maximum_lane = lane;
    }
    if (!align4_u32(bank_cursor, &bank_end) || bank_end != parsed.bank_payloads_size ||
        !zero_span(bytes, expected_bank + bank_cursor,
                   expected_bank + parsed.bank_payloads_size) ||
        parsed.workspace_lane_stride != maximum_lane ||
        maximum_lane > (UINT32_MAX - 3U) / 2U ||
        parsed.maximum_scratch != 3U + 2U * maximum_lane)
        return false;
    *out = parsed;
    return true;
}

bool sm64_saturn_actor_bundle_variant(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      sm64_saturn_actor_bundle_variant_t *out)
{
    uint16_t low = 0U, high;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (view == NULL || out == NULL || family_ordinal == 0U || model_id == 0U)
        return false;
    high = view->variant_count;
    while (low < high) {
        uint16_t middle = (uint16_t)(low + (uint16_t)((high - low) / 2U));
        sm64_saturn_actor_bundle_variant_t found;
        if (!read_variant_record(view, middle, &found)) return false;
        if (found.family_ordinal < family_ordinal ||
            (found.family_ordinal == family_ordinal && found.model_id < model_id))
            low = (uint16_t)(middle + 1U);
        else if (found.family_ordinal > family_ordinal || found.model_id > model_id)
            high = middle;
        else {
            *out = found;
            return true;
        }
    }
    return false;
}

bool sm64_saturn_actor_bundle_resolve(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      uint32_t actor_bank_id,
                                      const uint32_t source_hash_words[8],
                                      sm64_saturn_actor_bank_view_t *out)
{
    sm64_saturn_actor_bundle_variant_t variant;
    sm64_saturn_actor_bank_view_t parsed;
    const uint8_t *bytes;
    uint16_t word;
    uint32_t record_size, lane, usable, reserved;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (view == NULL || out == NULL || source_hash_words == NULL ||
        actor_bank_id == 0U ||
        !sm64_saturn_actor_bundle_variant(view, family_ordinal, model_id, &variant) ||
        actor_bank_id != variant.source_hash_words[0])
        return false;
    for (word = 0U; word < 8U; word++)
        if (source_hash_words[word] != variant.source_hash_words[word]) return false;
    if (!span_u32(variant.bank_offset, variant.bank_size, view->bank_payloads_size) ||
        !span_u32(view->bank_payloads_offset, view->bank_payloads_size, view->byte_count) ||
        variant.bank_size < SM64_SATURN_ACTOR_BANK_HEADER_SIZE)
        return false;
    bytes = view->bytes + view->bank_payloads_offset + variant.bank_offset;
    memset(&parsed, 0, sizeof(parsed));
    parsed.bytes = bytes;
    parsed.byte_count = variant.bank_size;
    parsed.bank.magic = read_be32(bytes);
    parsed.bank.version = read_be16(bytes + 4U);
    parsed.bank.family_id = read_be16(bytes + 6U);
    parsed.bank.model_id = read_be16(bytes + 8U);
    parsed.bank.joint_count = read_be16(bytes + 10U);
    parsed.bank.animation_count = read_be16(bytes + 12U);
    parsed.bank.meshlet_count = read_be16(bytes + 14U);
    parsed.bank.primitive_count = read_be16(bytes + 16U);
    parsed.bank.vertex_count = read_be16(bytes + 18U);
    parsed.bank.max_instances = read_be16(bytes + 20U);
    parsed.bank.feature_mask = read_be32(bytes + 22U);
    for (word = 0U; word < 8U; word++)
        parsed.bank.source_hash_words[word] = read_be32(
            bytes + SM64_SATURN_ACTOR_BANK_SOURCE_SHA256_OFFSET +
            (uint32_t)word * 4U);
    record_size = read_be16(bytes + 60U);
    parsed.records_offset = read_be32(bytes + 62U);
    parsed.indices_offset = read_be32(bytes + 66U);
    parsed.indices_size = read_be32(bytes + 70U);
    parsed.values_offset = read_be32(bytes + 74U);
    parsed.values_size = read_be32(bytes + 78U);
    parsed.vertices_offset = read_be32(bytes + 82U);
    parsed.vertices_size = read_be32(bytes + 86U);
    parsed.meshlets_offset = read_be32(bytes + 90U);
    parsed.meshlets_size = read_be32(bytes + 94U);
    parsed.max_scratch = read_be32(
        bytes + SM64_SATURN_ACTOR_BANK_MAXIMUM_SCRATCH_OFFSET);
    if (parsed.bank.magic != SM64_SATURN_ACTOR_BANK_MAGIC ||
        parsed.bank.version != SM64_SATURN_ACTOR_BANK_VERSION ||
        parsed.bank.family_id != family_ordinal || parsed.bank.model_id != model_id ||
        read_be16(bytes + 58U) != SM64_SATURN_ACTOR_BANK_HEADER_SIZE ||
        record_size != SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE ||
        !multiply_u32(parsed.bank.animation_count, record_size, &record_size) ||
        !span_u32(parsed.records_offset, record_size, variant.bank_size) ||
        !span_u32(parsed.indices_offset, parsed.indices_size, variant.bank_size) ||
        !span_u32(parsed.values_offset, parsed.values_size, variant.bank_size) ||
        !span_u32(parsed.vertices_offset, parsed.vertices_size, variant.bank_size) ||
        !span_u32(parsed.meshlets_offset, parsed.meshlets_size, variant.bank_size) ||
        parsed.meshlets_offset + parsed.meshlets_size != variant.bank_size ||
        !sm64_saturn_actor_bank_workspace_requirements(
            parsed.bank.vertex_count, parsed.bank.joint_count, &lane, &usable, &reserved) ||
        lane != variant.bank_lane_bytes || reserved != variant.bank_maximum_scratch ||
        parsed.max_scratch != reserved)
        return false;
    for (word = 0U; word < 8U; word++)
        if (parsed.bank.source_hash_words[word] != source_hash_words[word]) return false;
    *out = parsed;
    return true;
}
