#ifndef SM64_SATURN_ACTOR_BANK_H
#define SM64_SATURN_ACTOR_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SM64_SATURN_ACTOR_BANK_MAGIC 0x53363442UL
#define SM64_SATURN_ACTOR_BANK_VERSION 1U
#define SM64_SATURN_ACTOR_BANK_HEADER_SIZE 104U
#define SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE 16U

typedef struct sm64_saturn_actor_animation_record {
    uint32_t values_offset, indices_offset;
    uint16_t frame_count, joint_count, flags;
    int16_t y_translation_divisor;
} sm64_saturn_actor_animation_record_t;

typedef struct sm64_saturn_actor_bank {
    uint32_t magic;
    uint16_t version, family_id, model_id, joint_count;
    uint16_t animation_count, meshlet_count, primitive_count;
    uint16_t vertex_count, max_instances;
    uint32_t feature_mask, source_hash_words[8];
} sm64_saturn_actor_bank_t;

typedef struct sm64_saturn_actor_bank_view {
    const uint8_t *bytes;
    size_t byte_count;
    sm64_saturn_actor_bank_t bank;
    uint32_t records_offset;
    uint32_t indices_offset, indices_size;
    uint32_t values_offset, values_size;
    uint32_t vertices_offset, vertices_size;
    uint32_t meshlets_offset, meshlets_size;
    uint32_t max_scratch;
} sm64_saturn_actor_bank_view_t;

bool sm64_saturn_actor_bank_validate(const void *data, size_t byte_count,
                                     sm64_saturn_actor_bank_view_t *view);
bool sm64_saturn_actor_bank_animation(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    sm64_saturn_actor_animation_record_t *record);
bool sm64_saturn_actor_bank_sample_channel(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    uint16_t frame, uint16_t channel, int16_t *sample);

#endif
