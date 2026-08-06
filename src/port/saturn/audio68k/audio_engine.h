#ifndef SM64_SATURN_AUDIO68K_AUDIO_ENGINE_H
#define SM64_SATURN_AUDIO68K_AUDIO_ENGINE_H

#include "sequence_vm.h"
#include "voice_allocator.h"

typedef struct sm64_saturn_audio_note_binding {
    uint32_t package_generation;
    uint32_t sound_ram_offset;
    uint16_t sample_count;
    uint16_t loop_start;
    uint16_t sample_rate;
    uint16_t tuning_q12;
    uint16_t sustain_q15;
    int16_t root_note;
    int16_t pan;
    uint8_t priority;
    uint8_t source_class;
    uint8_t loop;
    uint8_t attack_ticks;
    uint8_t decay_ticks;
    uint8_t release_ticks;
} sm64_saturn_audio_note_binding_t;

typedef struct sm64_saturn_audio_engine {
    sm64_saturn_voice_allocator_t allocator;
    uint32_t active_package_generation;
    uint32_t last_service_generation;
    uint32_t malformed_events;
    uint32_t duplicate_generations;
    bool has_service_generation;
} sm64_saturn_audio_engine_t;

void sm64_saturn_audio_engine_init(sm64_saturn_audio_engine_t *engine,
                                   uint32_t active_package_generation);
bool sm64_saturn_audio_engine_consume_sequence_event(
    sm64_saturn_audio_engine_t *engine, uint32_t event_generation,
    const sm64_saturn_sequence_vm_event_t *event,
    const sm64_saturn_audio_note_binding_t *binding,
    sm64_saturn_voice_allocation_t *allocation);
bool sm64_saturn_audio_engine_service_generation(
    sm64_saturn_audio_engine_t *engine, uint32_t generation);

#endif
