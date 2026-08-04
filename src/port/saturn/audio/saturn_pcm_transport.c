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

static bool sm64_saturn_pcm_opcode_is_valid(sm64_saturn_pcm_opcode_t opcode)
{
    return opcode == SM64_SATURN_PCM_OPCODE_PLAY ||
           opcode == SM64_SATURN_PCM_OPCODE_STOP_ALL ||
           opcode == SM64_SATURN_PCM_OPCODE_SET_MASTER;
}

void sm64_saturn_pcm_transport_init(sm64_saturn_pcm_transport_t *transport,
                                    volatile uint8_t *sound_ram)
{
    if (transport == NULL) {
        return;
    }
    transport->sound_ram = sound_ram;
    transport->enqueued = 0U;
    transport->dropped = 0U;
    transport->high_water = 0U;
}

bool sm64_saturn_pcm_enqueue(sm64_saturn_pcm_transport_t *transport,
                            sm64_saturn_pcm_opcode_t opcode,
                            const uint16_t words[7])
{
    volatile uint8_t *ram;
    uint16_t producer;
    uint16_t consumer;
    uint16_t next;
    uint16_t base;
    uint16_t occupancy;
    uint16_t i;

    if (transport == NULL) {
        return false;
    }
    ram = transport->sound_ram;
    if (ram == NULL || words == NULL || !sm64_saturn_pcm_opcode_is_valid(opcode)) {
        transport->dropped++;
        return false;
    }

    producer = sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET);
    consumer = sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_CONSUMER_OFFSET);
    if (producer >= SM64_SATURN_PCM_RING_COUNT ||
        consumer >= SM64_SATURN_PCM_RING_COUNT) {
        transport->dropped++;
        return false;
    }

    next = sm64_saturn_pcm_ring_next(producer);
    if (next == consumer) {
        transport->dropped++;
        return false;
    }

    base = (uint16_t)(SM64_SATURN_PCM_RING_OFFSET +
                      producer * SM64_SATURN_PCM_COMMAND_BYTES);
    sm64_saturn_pcm_put_be16(ram, base, (uint16_t)opcode);
    SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(base);
    for (i = 0U; i < 7U; ++i) {
        const uint16_t word_offset = (uint16_t)(base + 2U + i * 2U);
        sm64_saturn_pcm_put_be16(ram, word_offset, words[i]);
        SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(word_offset);
    }
    SM64_SATURN_PCM_PUBLISH_BARRIER();
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, next);
    SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(
        SM64_SATURN_PCM_PRODUCER_OFFSET);

    occupancy = (uint16_t)((next + SM64_SATURN_PCM_RING_COUNT - consumer) %
                           SM64_SATURN_PCM_RING_COUNT);
    if (occupancy > transport->high_water) {
        transport->high_water = occupancy;
    }
    transport->enqueued++;
    return true;
}
