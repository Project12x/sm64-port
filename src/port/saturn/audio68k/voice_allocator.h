#ifndef SM64_SATURN_AUDIO68K_VOICE_ALLOCATOR_H
#define SM64_SATURN_AUDIO68K_VOICE_ALLOCATOR_H

#include "desired_voice.h"

typedef struct sm64_saturn_voice_entry {
    sm64_saturn_voice_request_t request;
    sm64_saturn_desired_voice_t desired;
    uint16_t lifetime_remaining;
} sm64_saturn_voice_entry_t;

typedef struct sm64_saturn_voice_allocator {
    sm64_saturn_voice_entry_t voices[SM64_SATURN_SEMANTIC_VOICE_COUNT];
    uint32_t next_age;
    uint32_t dropped_sfx;
    uint32_t dropped_music;
    uint32_t steals;
    uint32_t releases;
} sm64_saturn_voice_allocator_t;

typedef struct sm64_saturn_voice_allocation {
    uint32_t displaced_note_id;
    uint8_t voice_index;
    bool stolen;
} sm64_saturn_voice_allocation_t;

void sm64_saturn_voice_allocator_init(sm64_saturn_voice_allocator_t *allocator);
bool sm64_saturn_voice_allocator_start(
    sm64_saturn_voice_allocator_t *allocator,
    const sm64_saturn_voice_request_t *request,
    sm64_saturn_voice_allocation_t *allocation);
bool sm64_saturn_voice_allocator_release(
    sm64_saturn_voice_allocator_t *allocator, uint32_t note_id);
void sm64_saturn_voice_allocator_tick(sm64_saturn_voice_allocator_t *allocator);
uint8_t sm64_saturn_voice_allocator_active_count(
    const sm64_saturn_voice_allocator_t *allocator);

#endif
