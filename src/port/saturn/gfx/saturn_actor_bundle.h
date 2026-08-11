#ifndef SM64_SATURN_ACTOR_BUNDLE_H
#define SM64_SATURN_ACTOR_BUNDLE_H

#include "saturn_actor_bank.h"

#include <stdbool.h>
#include <stdint.h>

#define SM64_SATURN_ACTOR_BUNDLE_MAGIC 0x53363446UL
#define SM64_SATURN_ACTOR_BUNDLE_VERSION 3U
#define SM64_SATURN_ACTOR_BUNDLE_HEADER_SIZE 96U
#define SM64_SATURN_ACTOR_BUNDLE_FAMILY_RECORD_SIZE 64U
#define SM64_SATURN_ACTOR_BUNDLE_VARIANT_RECORD_SIZE 88U
#define SM64_SATURN_ACTOR_BUNDLE_MAX_FAMILIES 64U
#define SM64_SATURN_ACTOR_BUNDLE_MAX_VARIANTS 128U

typedef struct sm64_saturn_actor_bundle_view {
    const uint8_t *bytes;
    uint32_t byte_count, family_records_offset, variant_records_offset;
    uint32_t metadata_offset, metadata_size, bank_payloads_offset;
    uint32_t bank_payloads_size, workspace_lane_stride, maximum_scratch;
    uint32_t package_generation, content_hash_words[8];
    uint16_t family_count, variant_count;
} sm64_saturn_actor_bundle_view_t;

typedef struct sm64_saturn_actor_bundle_variant {
    uint16_t family_ordinal, model_id;
    uint32_t bank_offset, bank_size, bank_lane_bytes, bank_maximum_scratch;
    uint32_t payload_hash_words[8], source_hash_words[8];
} sm64_saturn_actor_bundle_variant_t;

bool sm64_saturn_actor_bundle_validate(const void *bytes, uint32_t byte_count,
                                       sm64_saturn_actor_bundle_view_t *out);
bool sm64_saturn_actor_bundle_variant(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      sm64_saturn_actor_bundle_variant_t *out);
bool sm64_saturn_actor_bundle_resolve(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      uint32_t actor_bank_id,
                                      const uint32_t source_hash_words[8],
                                      sm64_saturn_actor_bank_view_t *out);

#endif
