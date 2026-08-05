#ifndef SM64_SATURN_PCM_TRANSPORT_H
#define SM64_SATURN_PCM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

typedef struct sm64_saturn_pcm_transport {
    volatile uint8_t *sound_ram;
    uint32_t control_enqueued;
    uint32_t sfx_enqueued;
    uint32_t control_saturated;
    uint32_t sfx_saturated;
    uint32_t protocol_faults;
    uint16_t control_high_water;
    uint16_t sfx_high_water;
} sm64_saturn_pcm_transport_t;

void sm64_saturn_pcm_transport_init(sm64_saturn_pcm_transport_t *transport,
                                    volatile uint8_t *sound_ram);

bool sm64_saturn_audio_control_enqueue(sm64_saturn_pcm_transport_t *transport,
                                       sm64_saturn_audio_opcode_t opcode,
                                       const uint16_t words[7]);
bool sm64_saturn_audio_sfx_enqueue(sm64_saturn_pcm_transport_t *transport,
                                   sm64_saturn_audio_opcode_t opcode,
                                   const uint16_t words[7]);

#endif
