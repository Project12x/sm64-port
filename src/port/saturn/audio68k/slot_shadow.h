#ifndef SM64_SATURN_AUDIO68K_SLOT_SHADOW_H
#define SM64_SATURN_AUDIO68K_SLOT_SHADOW_H

#include "desired_voice.h"

enum {
    SM64_SATURN_SLOT_MAX_COMMANDS = 10U,
};

typedef enum sm64_saturn_slot_field {
    SM64_SATURN_SLOT_FIELD_KEY_CONTROL = 0,
    SM64_SATURN_SLOT_FIELD_START_ADDRESS = 1,
    SM64_SATURN_SLOT_FIELD_LOOP_START = 2,
    SM64_SATURN_SLOT_FIELD_LOOP_END = 3,
    SM64_SATURN_SLOT_FIELD_ENVELOPE = 4,
    SM64_SATURN_SLOT_FIELD_RELEASE = 5,
    SM64_SATURN_SLOT_FIELD_ATTENUATION = 6,
    SM64_SATURN_SLOT_FIELD_PITCH = 7,
    SM64_SATURN_SLOT_FIELD_PAN_SEND = 8,
} sm64_saturn_slot_field_t;

typedef struct sm64_saturn_slot_command {
    uint16_t value;
    uint8_t slot;
    uint8_t field;
} sm64_saturn_slot_command_t;

typedef struct sm64_saturn_slot_state {
    uint32_t package_generation;
    uint32_t note_id;
    uint16_t start_address_low;
    uint16_t loop_start;
    uint16_t loop_end;
    uint16_t envelope_word;
    uint16_t release_word;
    uint16_t attenuation;
    uint16_t pitch_word;
    uint16_t pan_send_word;
    uint16_t key_word;
    bool active;
} sm64_saturn_slot_state_t;

typedef struct sm64_saturn_slot_shadow {
    sm64_saturn_slot_state_t slots[SM64_SATURN_SCSP_SLOT_COUNT];
    uint32_t commands_emitted;
    uint32_t stale_generations;
    uint32_t capacity_faults;
} sm64_saturn_slot_shadow_t;

void sm64_saturn_slot_shadow_init(sm64_saturn_slot_shadow_t *shadow);
bool sm64_saturn_slot_shadow_diff(
    sm64_saturn_slot_shadow_t *shadow, uint8_t slot,
    const sm64_saturn_desired_voice_t *desired,
    uint32_t active_package_generation,
    sm64_saturn_slot_command_t *commands, uint8_t command_capacity,
    uint8_t *command_count);

#endif
