#include "saturn_audio_spatial.h"

#include <stddef.h>
#include <string.h>

enum {
    SOUND_NO_VOLUME_LOSS_BITS = 0x01000000U,
    SOUND_VIBRATO_BITS = 0x02000000U,
    SOUND_NO_PRIORITY_LOSS_BITS = 0x04000000U,
    SOUND_CONSTANT_FREQUENCY_BITS = 0x08000000U,
    SOUND_BANK_MOVING_VALUE = 1U,
    SOUND_BANK_MENU_VALUE = 7U,
    SOUND_MOVING_FLYING_ID = 0x17U,
};

#define AUDIO_MAX_DISTANCE 22000.0f
#define SOURCE_TOKEN_SLOT_MASK 0x007FU
#define SOURCE_TOKEN_GENERATION_MAX 0x01FFU

static uint16_t source_token(uint16_t slot, uint16_t generation)
{
    return (uint16_t)((generation << 7) | (slot + 1U));
}

static uint16_t source_token_slot(uint16_t token)
{
    const uint16_t encoded = (uint16_t)(token & SOURCE_TOKEN_SLOT_MASK);
    return encoded == 0U ? UINT16_MAX : (uint16_t)(encoded - 1U);
}

static float spatial_abs(float value)
{
    return value < 0.0f ? -value : value;
}

static float spatial_sqrt(float value)
{
    float estimate;
    uint8_t i;

    if (value <= 0.0f) {
        return 0.0f;
    }
    estimate = value > 1.0f ? value : 1.0f;
    for (i = 0U; i < 24U; ++i) {
        estimate = 0.5f * (estimate + value / estimate);
    }
    return estimate;
}

static uint8_t quantize_u8(float value, float scale)
{
    float scaled;
    if (value <= 0.0f) {
        return 0U;
    }
    if (value >= 1.0f) {
        return (uint8_t)scale;
    }
    scaled = value * scale + 0.5f;
    return (uint8_t)scaled;
}

void sm64_saturn_audio_spatial_init(sm64_saturn_audio_spatial_table_t *table)
{
    if (table != NULL) {
        memset(table, 0, sizeof(*table));
    }
}

uint16_t sm64_saturn_audio_spatial_find(
    const sm64_saturn_audio_spatial_table_t *table, const float *identity)
{
    uint16_t i;
    if (table == NULL || identity == NULL) {
        return 0U;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SOURCE_CAPACITY; ++i) {
        if (table->entries[i].active && table->entries[i].identity == identity) {
            return source_token(i, table->entries[i].token_generation);
        }
    }
    return 0U;
}

uint16_t sm64_saturn_audio_spatial_acquire(
    sm64_saturn_audio_spatial_table_t *table, const float *identity,
    uint16_t package_generation)
{
    uint16_t token;
    uint16_t i;

    token = sm64_saturn_audio_spatial_find(table, identity);
    if (token != 0U) {
        return table->entries[source_token_slot(token)].package_generation ==
                       package_generation
                   ? token
                   : 0U;
    }
    if (table == NULL || identity == NULL) {
        return 0U;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SOURCE_CAPACITY; ++i) {
        sm64_saturn_audio_source_entry_t *entry = &table->entries[i];
        if (!entry->active && !entry->retired) {
            if (entry->token_generation == SOURCE_TOKEN_GENERATION_MAX) {
                entry->retired = true;
                continue;
            }
            entry->active = true;
            entry->identity = identity;
            entry->package_generation = package_generation;
            entry->token_generation = (uint16_t)(entry->token_generation + 1U);
            return source_token(i, entry->token_generation);
        }
    }
    return 0U;
}

bool sm64_saturn_audio_spatial_release(sm64_saturn_audio_spatial_table_t *table,
                                       const float *identity)
{
    uint16_t token = sm64_saturn_audio_spatial_find(table, identity);
    if (token == 0U) {
        return false;
    }
    token = source_token_slot(token);
    if (token >= SM64_SATURN_AUDIO_SOURCE_CAPACITY) {
        return false;
    }
    table->entries[token].active = false;
    table->entries[token].identity = NULL;
    table->entries[token].package_generation = 0U;
    return true;
}

uint16_t sm64_saturn_audio_spatial_generation(
    const sm64_saturn_audio_spatial_table_t *table, uint16_t token)
{
    uint16_t slot;
    uint16_t generation;
    if (table == NULL || token == 0U) {
        return 0U;
    }
    slot = source_token_slot(token);
    generation = (uint16_t)(token >> 7);
    if (slot >= SM64_SATURN_AUDIO_SOURCE_CAPACITY ||
        !table->entries[slot].active ||
        table->entries[slot].token_generation != generation) {
        return 0U;
    }
    return generation;
}

const float *sm64_saturn_audio_spatial_resolve(
    const sm64_saturn_audio_spatial_table_t *table, uint16_t token,
    uint16_t package_generation)
{
    uint16_t slot;
    if (sm64_saturn_audio_spatial_generation(table, token) == 0U) {
        return NULL;
    }
    slot = source_token_slot(token);
    if (table->entries[slot].package_generation != package_generation) {
        return NULL;
    }
    return table->entries[slot].identity;
}

void sm64_saturn_audio_spatial_quantize(
    uint32_t sound_bits, uint8_t bank, uint8_t moving_speed,
    uint16_t acoustic_reach, uint32_t audio_random, float x, float y, float z,
    sm64_saturn_audio_spatial_params_t *out)
{
    const float abs_x = spatial_abs(x) > AUDIO_MAX_DISTANCE
                            ? AUDIO_MAX_DISTANCE
                            : spatial_abs(x);
    const float abs_z = spatial_abs(z) > AUDIO_MAX_DISTANCE
                            ? AUDIO_MAX_DISTANCE
                            : spatial_abs(z);
    float distance;
    float volume;
    float pan;
    float pitch;
    float intensity;
    float max_sound_distance;
    float volume_range;
    const uint8_t requested_priority = (uint8_t)(sound_bits >> 8);
    const uint8_t sound_identifier = (uint8_t)(sound_bits >> 16);

    if (out == NULL) {
        return;
    }
    distance = spatial_sqrt(x * x + y * y + z * z);
    volume_range = bank < 3U ? 0.9f : 0.8f;
    if ((sound_bits & SOUND_NO_VOLUME_LOSS_BITS) != 0U) {
        intensity = 1.0f;
    } else if (distance > AUDIO_MAX_DISTANCE) {
        intensity = 0.0f;
    } else {
        if (acoustic_reach == 0U) {
            acoustic_reach = 20000U;
        }
        max_sound_distance = (float)acoustic_reach /
                             (bank < 3U ? 2.0f : 3.0f);
        if (max_sound_distance < distance) {
            intensity = ((AUDIO_MAX_DISTANCE - distance) /
                         (AUDIO_MAX_DISTANCE - max_sound_distance)) *
                        (1.0f - volume_range);
        } else {
            intensity = 1.0f - distance / max_sound_distance * volume_range;
        }
        if ((sound_bits & SOUND_VIBRATO_BITS) != 0U && intensity >= 0.08f) {
            intensity -= (float)(audio_random & 0x0FU) / 192.0f;
        }
    }
    volume = volume_range * intensity * intensity + 1.0f - volume_range;

    if (x == 0.0f && z == 0.0f) {
        pan = 0.5f;
    } else if (x >= 0.0f && abs_x >= abs_z) {
        pan = 1.0f - (2.0f * AUDIO_MAX_DISTANCE - abs_x) /
                         (3.0f * (2.0f * AUDIO_MAX_DISTANCE - abs_z));
    } else if (x < 0.0f && abs_x > abs_z) {
        pan = (2.0f * AUDIO_MAX_DISTANCE - abs_x) /
              (3.0f * (2.0f * AUDIO_MAX_DISTANCE - abs_z));
    } else {
        pan = 0.5f + x / (6.0f * abs_z);
    }
    if (pan < 0.0f) {
        pan = 0.0f;
    } else if (pan > 1.0f) {
        pan = 1.0f;
    }

    if (bank == SOUND_BANK_MENU_VALUE ||
        (bank == SOUND_BANK_MOVING_VALUE &&
         (sound_bits & SOUND_CONSTANT_FREQUENCY_BITS) != 0U)) {
        volume = 1.0f;
        pan = 0.5f;
        pitch = 1.0f;
    } else if ((sound_bits & SOUND_CONSTANT_FREQUENCY_BITS) != 0U) {
        pitch = 1.0f;
    } else {
        float amount = distance / AUDIO_MAX_DISTANCE;
        if ((sound_bits & SOUND_VIBRATO_BITS) != 0U) {
            amount += (float)(audio_random & 0xFFU) / 64.0f;
        }
        pitch = 1.0f + amount / 15.0f;
        if (bank == SOUND_BANK_MOVING_VALUE) {
            pitch += (float)moving_speed /
                     (sound_identifier == SOUND_MOVING_FLYING_ID
                          ? 80.0f
                          : 400.0f);
            if (moving_speed <= 8U) {
                volume *= ((float)moving_speed + 8.0f) / 16.0f;
            }
        }
    }
    if (pitch < 0.0f) {
        pitch = 0.0f;
    } else if (pitch > 15.999f) {
        pitch = 15.999f;
    }

    out->volume = quantize_u8(volume, 255.0f);
    out->pan = quantize_u8(pan, 127.0f);
    out->pitch = (uint16_t)(pitch * 4096.0f + 0.5f);
    out->priority_score =
        (uint32_t)(0x4CU * (uint32_t)(0xFFU - requested_priority));
    if ((sound_bits & SOUND_NO_PRIORITY_LOSS_BITS) == 0U) {
        out->priority_score += (uint32_t)distance;
        if (z > 0.0f) {
            out->priority_score += (uint32_t)(z / 6.0f);
        }
    }
}

void sm64_saturn_audio_spatial_encode_play_refresh(
    const sm64_saturn_audio_play_refresh_t *refresh, uint16_t words[7])
{
    if (refresh == NULL || words == NULL) {
        return;
    }
    words[0] = (uint16_t)(refresh->sound_bits >> 16);
    words[1] = (uint16_t)refresh->sound_bits;
    words[2] = refresh->source_token;
    words[3] = refresh->package_generation;
    words[4] = (uint16_t)(((uint16_t)refresh->volume << 8) | refresh->pan);
    words[5] = refresh->pitch;
    words[6] = refresh->freshness_generation;
}
