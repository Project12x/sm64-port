#ifndef SM64_SATURN_PCM_VOICE_H
#define SM64_SATURN_PCM_VOICE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_PCM_VOICE_COUNT = 4U,
    SM64_SATURN_PCM_COMMANDS_PER_POLL = 8U,
};

typedef struct sm64_saturn_pcm_sample {
    uint32_t sound_ram_offset;
    uint16_t sample_count;
    uint16_t sample_rate;
    uint16_t default_volume;
    uint16_t flags;
} sm64_saturn_pcm_sample_t;

typedef struct sm64_saturn_pcm_voice {
    bool active;
    uint16_t sample_id;
    uint16_t volume;
    int16_t pan;
    uint16_t generation;
} sm64_saturn_pcm_voice_t;

typedef struct sm64_saturn_pcm_voice_state {
    sm64_saturn_pcm_voice_t voices[SM64_SATURN_PCM_VOICE_COUNT];
    uint32_t commands_consumed;
    uint32_t voices_started;
    uint32_t keyoffs;
    uint32_t unknown_opcodes;
    uint32_t invalid_samples;
    uint32_t protocol_faults;
    uint16_t master_volume;
    uint16_t last_opcode;
    uint16_t active_slot;
    uint16_t next_slot;
} sm64_saturn_pcm_voice_state_t;

void sm64_saturn_pcm_voice_state_init(sm64_saturn_pcm_voice_state_t *state);
uint16_t sm64_saturn_pcm_proof_sample_count(void);
const sm64_saturn_pcm_sample_t *sm64_saturn_pcm_proof_sample(uint16_t sample_id);
uint16_t sm64_saturn_pcm68k_consume(volatile uint8_t *sound_ram,
                                   sm64_saturn_pcm_voice_state_t *state);

#endif
