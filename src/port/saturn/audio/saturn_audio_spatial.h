/* SH-2-local source identity and spatial quantization.  Raw source pointers
 * never leave this table and never appear in the semantic wire record. */
#ifndef SM64_SATURN_AUDIO_SPATIAL_H
#define SM64_SATURN_AUDIO_SPATIAL_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_audio_policy.h"

enum {
    SM64_SATURN_AUDIO_SOURCE_CAPACITY = 64U,
};

typedef struct sm64_saturn_audio_source_entry {
    const float *identity;
    uint16_t package_generation;
    uint16_t token_generation;
    bool active;
} sm64_saturn_audio_source_entry_t;

typedef struct sm64_saturn_audio_spatial_table {
    sm64_saturn_audio_source_entry_t
        entries[SM64_SATURN_AUDIO_SOURCE_CAPACITY];
} sm64_saturn_audio_spatial_table_t;

typedef struct sm64_saturn_audio_spatial_params {
    uint8_t volume;
    uint8_t pan;
    uint16_t pitch;
} sm64_saturn_audio_spatial_params_t;

void sm64_saturn_audio_spatial_init(sm64_saturn_audio_spatial_table_t *table);
uint16_t sm64_saturn_audio_spatial_acquire(
    sm64_saturn_audio_spatial_table_t *table, const float *identity,
    uint16_t package_generation);
uint16_t sm64_saturn_audio_spatial_find(
    const sm64_saturn_audio_spatial_table_t *table, const float *identity);
bool sm64_saturn_audio_spatial_release(sm64_saturn_audio_spatial_table_t *table,
                                       const float *identity);
uint16_t sm64_saturn_audio_spatial_generation(
    const sm64_saturn_audio_spatial_table_t *table, uint16_t token);
void sm64_saturn_audio_spatial_quantize(uint32_t sound_bits, uint8_t bank,
                                        uint8_t moving_speed, float x, float y,
                                        float z,
                                        sm64_saturn_audio_spatial_params_t *out);
void sm64_saturn_audio_spatial_encode_play_refresh(
    const sm64_saturn_audio_play_refresh_t *refresh, uint16_t words[7]);

#endif
