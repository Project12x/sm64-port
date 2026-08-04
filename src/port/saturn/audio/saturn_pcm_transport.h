#ifndef SM64_SATURN_PCM_TRANSPORT_H
#define SM64_SATURN_PCM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

typedef struct sm64_saturn_pcm_transport {
    volatile uint8_t *sound_ram;
    uint32_t enqueued;
    uint32_t dropped;
    uint16_t high_water;
} sm64_saturn_pcm_transport_t;

void sm64_saturn_pcm_transport_init(sm64_saturn_pcm_transport_t *transport,
                                    volatile uint8_t *sound_ram);

bool sm64_saturn_pcm_enqueue(sm64_saturn_pcm_transport_t *transport,
                            sm64_saturn_pcm_opcode_t opcode,
                            const uint16_t words[7]);

#endif
