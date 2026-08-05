#include "saturn_pcm_transport.h"

#include <stddef.h>

#ifndef SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER
#define SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(offset) ((void)(offset))
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SM64_SATURN_PCM_PUBLISH_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SM64_SATURN_PCM_PUBLISH_BARRIER() ((void)0)
#endif

typedef struct sm64_saturn_pcm_ring_config {
    uint16_t producer_offset;
    uint16_t consumer_offset;
    uint16_t ring_offset;
    uint16_t ring_count;
    uint16_t saturated_offset;
    bool control;
} sm64_saturn_pcm_ring_config_t;

static const sm64_saturn_pcm_ring_config_t s_producer_control_ring = {
    SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET,
    SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_COUNT,
    SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET,
    true,
};

static const sm64_saturn_pcm_ring_config_t s_producer_sfx_ring = {
    SM64_SATURN_PCM_SFX_PRODUCER_OFFSET,
    SM64_SATURN_PCM_SFX_CONSUMER_OFFSET,
    SM64_SATURN_PCM_SFX_RING_OFFSET,
    SM64_SATURN_PCM_SFX_RING_COUNT,
    SM64_SATURN_PCM_SFX_SATURATED_OFFSET,
    false,
};

void sm64_saturn_pcm_transport_init(sm64_saturn_pcm_transport_t *transport,
                                    volatile uint8_t *sound_ram)
{
    if (transport == NULL) {
        return;
    }
    transport->sound_ram = sound_ram;
    transport->control_enqueued = 0U;
    transport->sfx_enqueued = 0U;
    transport->control_saturated = 0U;
    transport->sfx_saturated = 0U;
    transport->protocol_faults = 0U;
    transport->control_high_water = 0U;
    transport->sfx_high_water = 0U;
}

static bool sm64_saturn_pcm_protocol_is_v2(const volatile uint8_t *ram)
{
    return sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_MAGIC &&
           sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_VERSION;
}

static bool sm64_saturn_audio_enqueue(
    sm64_saturn_pcm_transport_t *transport,
    const sm64_saturn_pcm_ring_config_t *ring,
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7])
{
    volatile uint8_t *ram;
    uint16_t producer;
    uint16_t consumer;
    uint16_t occupancy;
    uint16_t next;
    uint16_t base;
    uint16_t i;

    if (transport == NULL) {
        return false;
    }
    ram = transport->sound_ram;
    if (ram == NULL || words == NULL ||
        (ring->control ? !sm64_saturn_audio_opcode_is_control(opcode)
                       : !sm64_saturn_audio_opcode_is_sfx(opcode))) {
        transport->protocol_faults++;
        return false;
    }
    if (!sm64_saturn_pcm_protocol_is_v2(ram)) {
        transport->protocol_faults++;
        return false;
    }

    producer = sm64_saturn_pcm_get_be16(ram, ring->producer_offset);
    consumer = sm64_saturn_pcm_get_be16(ram, ring->consumer_offset);
    if (!sm64_saturn_pcm_ring_cursor_is_valid(producer, ring->ring_count) ||
        !sm64_saturn_pcm_ring_cursor_is_valid(consumer, ring->ring_count)) {
        transport->protocol_faults++;
        return false;
    }
    occupancy = sm64_saturn_pcm_ring_occupancy(producer, consumer,
                                               ring->ring_count);
    if (occupancy > ring->ring_count) {
        transport->protocol_faults++;
        return false;
    }
    if (occupancy == ring->ring_count) {
        uint16_t saturated = sm64_saturn_pcm_get_be16(
            ram, ring->saturated_offset);
        saturated = (uint16_t)(saturated + 1U);
        sm64_saturn_pcm_put_be16(ram, ring->saturated_offset, saturated);
        if (ring->control) {
            transport->control_saturated++;
        } else {
            transport->sfx_saturated++;
        }
        return false;
    }

    base = (uint16_t)(ring->ring_offset +
        sm64_saturn_pcm_ring_cursor_slot(producer, ring->ring_count) *
            SM64_SATURN_PCM_COMMAND_BYTES);
    sm64_saturn_pcm_put_be16(ram, base, (uint16_t)opcode);
    SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(base);
    for (i = 0U; i < 7U; ++i) {
        const uint16_t word_offset = (uint16_t)(base + 2U + i * 2U);
        sm64_saturn_pcm_put_be16(ram, word_offset, words[i]);
        SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(word_offset);
    }
    next = sm64_saturn_pcm_ring_cursor_next(producer, ring->ring_count);
    SM64_SATURN_PCM_PUBLISH_BARRIER();
    sm64_saturn_pcm_put_be16(ram, ring->producer_offset, next);
    SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(ring->producer_offset);

    occupancy++;
    if (ring->control) {
        transport->control_enqueued++;
        if (occupancy > transport->control_high_water) {
            transport->control_high_water = occupancy;
        }
    } else {
        transport->sfx_enqueued++;
        if (occupancy > transport->sfx_high_water) {
            transport->sfx_high_water = occupancy;
        }
    }
    return true;
}

bool sm64_saturn_audio_control_enqueue(sm64_saturn_pcm_transport_t *transport,
                                       sm64_saturn_audio_opcode_t opcode,
                                       const uint16_t words[7])
{
    return sm64_saturn_audio_enqueue(transport, &s_producer_control_ring, opcode,
                                     words);
}

bool sm64_saturn_audio_sfx_enqueue(sm64_saturn_pcm_transport_t *transport,
                                   sm64_saturn_audio_opcode_t opcode,
                                   const uint16_t words[7])
{
    return sm64_saturn_audio_enqueue(transport, &s_producer_sfx_ring, opcode,
                                     words);
}
