#include "saturn_pcm_transport.h"

#include <stddef.h>
#include <string.h>

#ifndef SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER
#define SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(offset) ((void)(offset))
#endif

#ifndef SM64_SATURN_PCM_STATUS_GENERATION_READ_OBSERVER
#define SM64_SATURN_PCM_STATUS_GENERATION_READ_OBSERVER(ram) ((void)(ram))
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
    uint16_t source_ring;
    bool control;
} sm64_saturn_pcm_ring_config_t;

static const sm64_saturn_pcm_ring_config_t s_producer_control_ring = {
    SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET,
    SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_COUNT,
    SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET,
    SM64_SATURN_AUDIO_RING_CONTROL,
    true,
};

static const sm64_saturn_pcm_ring_config_t s_producer_sfx_ring = {
    SM64_SATURN_PCM_SFX_PRODUCER_OFFSET,
    SM64_SATURN_PCM_SFX_CONSUMER_OFFSET,
    SM64_SATURN_PCM_SFX_RING_OFFSET,
    SM64_SATURN_PCM_SFX_RING_COUNT,
    SM64_SATURN_PCM_SFX_SATURATED_OFFSET,
    SM64_SATURN_AUDIO_RING_SFX,
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
    transport->completions_drained = 0U;
    transport->completion_protocol_faults = 0U;
    transport->ticket_busy = 0U;
    transport->control_high_water = 0U;
    transport->sfx_high_water = 0U;
    memset(transport->control_ticket_pending, 0,
           sizeof(transport->control_ticket_pending));
    memset(transport->sfx_ticket_pending, 0,
           sizeof(transport->sfx_ticket_pending));
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
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7],
    sm64_saturn_audio_ticket_t *ticket)
{
    volatile uint8_t *ram;
    uint16_t producer;
    uint16_t consumer;
    uint16_t occupancy;
    uint16_t next;
    uint16_t base;
    uint16_t i;
    uint16_t *pending;

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
    if (ticket != NULL &&
        (sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET) &
         SM64_SATURN_PCM_ABI_FLAG_COMPLETION) == 0U) {
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
        saturated = sm64_saturn_pcm_counter_saturating_increment(saturated);
        sm64_saturn_pcm_put_be16(ram, ring->saturated_offset, saturated);
        if (ring->control) {
            transport->control_saturated++;
        } else {
            transport->sfx_saturated++;
        }
        return false;
    }

    pending = ring->control ? transport->control_ticket_pending
                            : transport->sfx_ticket_pending;
    if (pending[producer] != 0U) {
        transport->ticket_busy++;
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

    if (ticket != NULL) {
        ticket->source_ring = ring->source_ring;
        ticket->cursor = producer;
        ticket->opcode = (uint16_t)opcode;
        pending[producer] = (uint16_t)opcode;
    }

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
    return sm64_saturn_audio_control_enqueue_ticket(transport, opcode, words,
                                                     NULL);
}

bool sm64_saturn_audio_sfx_enqueue(sm64_saturn_pcm_transport_t *transport,
                                   sm64_saturn_audio_opcode_t opcode,
                                   const uint16_t words[7])
{
    return sm64_saturn_audio_sfx_enqueue_ticket(transport, opcode, words,
                                                 NULL);
}

bool sm64_saturn_audio_control_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7],
    sm64_saturn_audio_ticket_t *ticket)
{
    return sm64_saturn_audio_enqueue(transport, &s_producer_control_ring,
                                     opcode, words, ticket);
}

bool sm64_saturn_audio_sfx_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7],
    sm64_saturn_audio_ticket_t *ticket)
{
    return sm64_saturn_audio_enqueue(transport, &s_producer_sfx_ring,
                                     opcode, words, ticket);
}

bool sm64_saturn_audio_play_refresh_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport, const uint16_t words[7],
    uint32_t package_generation, sm64_saturn_audio_ticket_t *ticket)
{
    uint16_t encoded[7];
    uint16_t epoch;

    if (transport == NULL || words == NULL || ticket == NULL ||
        !sm64_saturn_audio_play_refresh_epoch_from_generation(
            package_generation, &epoch)) {
        if (transport != NULL) {
            transport->protocol_faults++;
        }
        return false;
    }
    memcpy(encoded, words, sizeof(encoded));
    encoded[3] = epoch;
    return sm64_saturn_audio_sfx_enqueue_ticket(
        transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, encoded, ticket);
}

static uint16_t *sm64_saturn_audio_pending_entry(
    sm64_saturn_pcm_transport_t *transport,
    const sm64_saturn_audio_ticket_t *ticket)
{
    if (ticket->source_ring == SM64_SATURN_AUDIO_RING_CONTROL &&
        sm64_saturn_pcm_ring_cursor_is_valid(
            ticket->cursor, SM64_SATURN_PCM_CONTROL_RING_COUNT)) {
        return &transport->control_ticket_pending[ticket->cursor];
    }
    if (ticket->source_ring == SM64_SATURN_AUDIO_RING_SFX &&
        sm64_saturn_pcm_ring_cursor_is_valid(
            ticket->cursor, SM64_SATURN_PCM_SFX_RING_COUNT)) {
        return &transport->sfx_ticket_pending[ticket->cursor];
    }
    return NULL;
}

static bool sm64_saturn_audio_completion_record_is_valid(
    const sm64_saturn_audio_completion_t *completion)
{
    uint16_t ring_count;
    const sm64_saturn_audio_opcode_t opcode =
        (sm64_saturn_audio_opcode_t)completion->ticket.opcode;

    if (completion->ticket.source_ring == SM64_SATURN_AUDIO_RING_CONTROL) {
        ring_count = SM64_SATURN_PCM_CONTROL_RING_COUNT;
        if (!sm64_saturn_audio_opcode_is_control(opcode)) {
            return false;
        }
    } else if (completion->ticket.source_ring ==
               SM64_SATURN_AUDIO_RING_SFX) {
        ring_count = SM64_SATURN_PCM_SFX_RING_COUNT;
        if (!sm64_saturn_audio_opcode_is_sfx(opcode)) {
            return false;
        }
    } else {
        return false;
    }

    if (!sm64_saturn_pcm_ring_cursor_is_valid(completion->ticket.cursor,
                                               ring_count) ||
        completion->generation == 0U) {
        return false;
    }
    return sm64_saturn_audio_completion_status_opcode_is_legal(
        completion->ticket.source_ring, opcode, completion->status);
}

bool sm64_saturn_audio_completion_poll(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_completion_t *completion)
{
    volatile uint8_t *ram;
    sm64_saturn_audio_completion_t next_completion;
    uint16_t producer;
    uint16_t consumer;
    uint16_t occupancy;
    uint16_t base;
    uint16_t next;
    uint16_t *pending;

    if (transport == NULL) {
        return false;
    }
    ram = transport->sound_ram;
    if (ram == NULL || completion == NULL ||
        !sm64_saturn_pcm_protocol_is_v2(ram)) {
        transport->completion_protocol_faults++;
        return false;
    }
    if ((sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET) &
         SM64_SATURN_PCM_ABI_FLAG_COMPLETION) == 0U) {
        transport->completion_protocol_faults++;
        return false;
    }

    producer = sm64_saturn_pcm_get_be16(
        ram, SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET);
    consumer = sm64_saturn_pcm_get_be16(
        ram, SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET);
    if (!sm64_saturn_pcm_ring_cursor_is_valid(
            producer, SM64_SATURN_PCM_COMPLETION_RING_COUNT) ||
        !sm64_saturn_pcm_ring_cursor_is_valid(
            consumer, SM64_SATURN_PCM_COMPLETION_RING_COUNT)) {
        transport->completion_protocol_faults++;
        return false;
    }
    occupancy = sm64_saturn_pcm_ring_occupancy(
        producer, consumer, SM64_SATURN_PCM_COMPLETION_RING_COUNT);
    if (occupancy > SM64_SATURN_PCM_COMPLETION_RING_COUNT) {
        transport->completion_protocol_faults++;
        return false;
    }
    if (occupancy == 0U) {
        return false;
    }

    base = (uint16_t)(SM64_SATURN_PCM_COMPLETION_RING_OFFSET +
        sm64_saturn_pcm_ring_cursor_slot(
            consumer, SM64_SATURN_PCM_COMPLETION_RING_COUNT) *
            SM64_SATURN_PCM_COMPLETION_BYTES);
    next_completion.ticket.source_ring = sm64_saturn_pcm_get_be16(ram, base);
    next_completion.ticket.cursor = sm64_saturn_pcm_get_be16(
        ram, (uint16_t)(base + 2U));
    next_completion.ticket.opcode = sm64_saturn_pcm_get_be16(
        ram, (uint16_t)(base + 4U));
    next_completion.status = sm64_saturn_pcm_get_be16(
        ram, (uint16_t)(base + 6U));
    next_completion.generation = sm64_saturn_pcm_get_be32(
        ram, (uint16_t)(base + 8U));
    next_completion.detail = sm64_saturn_pcm_get_be16(
        ram, (uint16_t)(base + 12U));
    next_completion.service_tick = sm64_saturn_pcm_get_be16(
        ram, (uint16_t)(base + 14U));

    if (!sm64_saturn_audio_completion_record_is_valid(&next_completion)) {
        transport->completion_protocol_faults++;
        return false;
    }
    pending = sm64_saturn_audio_pending_entry(transport,
                                              &next_completion.ticket);
    if (pending == NULL || *pending != next_completion.ticket.opcode) {
        transport->completion_protocol_faults++;
        return false;
    }

    *completion = next_completion;
    next = sm64_saturn_pcm_ring_cursor_next(
        consumer, SM64_SATURN_PCM_COMPLETION_RING_COUNT);
    SM64_SATURN_PCM_PUBLISH_BARRIER();
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET, next);
    *pending = 0U;
    transport->completions_drained++;
    return true;
}

bool sm64_saturn_audio_completion_matches_ticket(
    const sm64_saturn_audio_completion_t *completion,
    const sm64_saturn_audio_ticket_t *ticket)
{
    return completion != NULL && ticket != NULL &&
           completion->ticket.source_ring == ticket->source_ring &&
           completion->ticket.cursor == ticket->cursor &&
           completion->ticket.opcode == ticket->opcode;
}

bool sm64_saturn_audio_completion_is_acceptance(
    const sm64_saturn_audio_completion_t *completion)
{
    return completion != NULL &&
           (completion->status == SM64_SATURN_AUDIO_COMPLETION_ACCEPTED ||
            completion->status == SM64_SATURN_AUDIO_COMPLETION_COMMITTED);
}

bool sm64_saturn_audio_completion_is_required(
    uint16_t source_ring, sm64_saturn_audio_opcode_t opcode,
    uint16_t status)
{
    if (!sm64_saturn_audio_completion_status_opcode_is_legal(
            source_ring, opcode, status)) {
        return false;
    }
    if (source_ring == SM64_SATURN_AUDIO_RING_CONTROL) {
        return sm64_saturn_audio_opcode_is_control(opcode);
    }
    return source_ring == SM64_SATURN_AUDIO_RING_SFX &&
           status != SM64_SATURN_AUDIO_COMPLETION_ACCEPTED;
}

bool sm64_saturn_audio_status_snapshot(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_status_snapshot_t *snapshot)
{
    volatile uint8_t *ram;
    uint16_t last_status;
    uint16_t flags_before;
    uint16_t flags_after;
    uint16_t attempt;
    uint16_t active_high;
    uint16_t active_low;

    if (transport == NULL) {
        return false;
    }
    ram = transport->sound_ram;
    if (ram == NULL || snapshot == NULL ||
        !sm64_saturn_pcm_protocol_is_v2(ram)) {
        transport->completion_protocol_faults++;
        return false;
    }
    for (attempt = 0U; attempt < 3U; ++attempt) {
        flags_before = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET);
        if ((flags_before & SM64_SATURN_PCM_ABI_FLAG_COMPLETION) == 0U ||
            (flags_before & SM64_SATURN_PCM_ABI_FLAG_STATUS_WRITING) != 0U) {
            continue;
        }
        last_status = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_LAST_COMPLETION_STATUS_OFFSET);
        active_high = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_ACTIVE_GENERATION_HIGH_OFFSET);
        SM64_SATURN_PCM_STATUS_GENERATION_READ_OBSERVER(ram);
        active_low = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_ACTIVE_GENERATION_LOW_OFFSET);
        snapshot->active_generation = ((uint32_t)active_high << 16) |
                                      (uint32_t)active_low;
        snapshot->prepared_generation = sm64_saturn_pcm_get_be32(
            ram, SM64_SATURN_PCM_PREPARED_GENERATION_HIGH_OFFSET);
        snapshot->completion_saturated = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_COMPLETION_SATURATED_OFFSET);
        snapshot->completion_protocol_faults = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_COMPLETION_PROTOCOL_FAULTS_OFFSET);
        snapshot->last_completion_status = last_status;
        snapshot->last_completion_detail = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_LAST_COMPLETION_DETAIL_OFFSET);
        snapshot->service_tick = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET);
        snapshot->active_voice_count = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET);
        snapshot->abi_flags = flags_before;
        SM64_SATURN_PCM_PUBLISH_BARRIER();
        flags_after = sm64_saturn_pcm_get_be16(
            ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET);
        if (flags_before == flags_after &&
            (last_status == 0U ||
             sm64_saturn_audio_completion_status_is_valid(last_status))) {
            return true;
        }
    }
    transport->completion_protocol_faults++;
    return false;
}
