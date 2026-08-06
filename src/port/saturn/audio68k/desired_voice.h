#ifndef SM64_SATURN_AUDIO68K_DESIRED_VOICE_H
#define SM64_SATURN_AUDIO68K_DESIRED_VOICE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_SEMANTIC_VOICE_COUNT = 20U,
    SM64_SATURN_SCSP_SLOT_COUNT = 32U,
    SM64_SATURN_SOUND_RAM_LIMIT = 0x80000U,
    SM64_SATURN_SLOT_KEY_EXECUTE = 0x1000U,
    SM64_SATURN_SLOT_KEY_ON = 0x0800U,
    SM64_SATURN_SLOT_PCM8 = 0x0010U,
    SM64_SATURN_SLOT_LOOP_NORMAL = 0x0020U,
};

typedef enum sm64_saturn_voice_class {
    SM64_SATURN_VOICE_CLASS_MUSIC = 1,
    SM64_SATURN_VOICE_CLASS_SFX = 2,
} sm64_saturn_voice_class_t;

typedef enum sm64_saturn_envelope_phase {
    SM64_SATURN_ENVELOPE_OFF = 0,
    SM64_SATURN_ENVELOPE_ATTACK = 1,
    SM64_SATURN_ENVELOPE_DECAY = 2,
    SM64_SATURN_ENVELOPE_SUSTAIN = 3,
    SM64_SATURN_ENVELOPE_RELEASE = 4,
} sm64_saturn_envelope_phase_t;

/* MC68000-local scalar note request. No pointer or native ABI crosses CPUs. */
typedef struct sm64_saturn_voice_request {
    uint32_t package_generation;
    uint32_t note_id;
    uint32_t sound_ram_offset;
    uint16_t sample_count;
    uint16_t loop_start;
    uint16_t sample_rate;
    uint16_t tuning_q12;
    uint16_t sustain_q15;
    uint16_t lifetime_ticks;
    int16_t root_note;
    int16_t note;
    int16_t pan;
    uint8_t velocity;
    uint8_t priority;
    uint8_t source_class;
    uint8_t loop;
    uint8_t attack_ticks;
    uint8_t decay_ticks;
    uint8_t release_ticks;
} sm64_saturn_voice_request_t;

typedef struct sm64_saturn_desired_voice {
    uint32_t package_generation;
    uint32_t note_id;
    uint32_t age;
    uint16_t start_address_low;
    uint16_t loop_start;
    uint16_t loop_end;
    uint16_t envelope_word;
    uint16_t release_word;
    uint16_t attenuation;
    uint16_t pitch_word;
    uint16_t pan_send_word;
    uint16_t key_word;
    uint16_t envelope_q15;
    uint16_t sustain_q15;
    uint16_t release_start_q15;
    uint8_t slot;
    uint8_t source_class;
    uint8_t priority;
    uint8_t phase;
    uint8_t phase_ticks;
    uint8_t attack_ticks;
    uint8_t decay_ticks;
    uint8_t release_ticks;
    uint8_t velocity;
    bool active;
    bool releasing;
} sm64_saturn_desired_voice_t;

bool sm64_saturn_desired_voice_request_valid(
    const sm64_saturn_voice_request_t *request);
bool sm64_saturn_desired_voice_pitch_word(uint16_t sample_rate,
                                          int16_t semitone_delta,
                                          uint16_t tuning_q12,
                                          uint16_t *pitch_word);
bool sm64_saturn_desired_voice_begin(sm64_saturn_desired_voice_t *desired,
                                     const sm64_saturn_voice_request_t *request,
                                     uint8_t slot, uint32_t age);
bool sm64_saturn_desired_voice_release(sm64_saturn_desired_voice_t *desired);
void sm64_saturn_desired_voice_tick(sm64_saturn_desired_voice_t *desired);

#endif
