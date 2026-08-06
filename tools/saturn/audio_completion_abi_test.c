#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_pcm_protocol.h"
#include "saturn_pcm_transport.h"

static bool s_replace_status_during_read;
static uint16_t s_status_read_observations;

static void observe_status_generation_read(volatile uint8_t *ram)
{
    s_status_read_observations++;
    if (s_replace_status_during_read) {
        s_replace_status_during_read = false;
        sm64_saturn_pcm_put_be16(
            ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET,
            sm64_saturn_pcm_status_publication_begin(
                SM64_SATURN_PCM_ABI_FLAG_COMPLETION));
        sm64_saturn_pcm_put_be16(
            ram, SM64_SATURN_PCM_ACTIVE_GENERATION_HIGH_OFFSET, 0x3333U);
        sm64_saturn_pcm_put_be16(
            ram, SM64_SATURN_PCM_ACTIVE_GENERATION_LOW_OFFSET, 0x4444U);
        sm64_saturn_pcm_put_be16(
            ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET,
            sm64_saturn_pcm_status_publication_finish(
                sm64_saturn_pcm_status_publication_begin(
                    SM64_SATURN_PCM_ABI_FLAG_COMPLETION)));
    }
}

#define SM64_SATURN_PCM_STATUS_GENERATION_READ_OBSERVER(ram) \
    observe_status_generation_read(ram)
#include "../../src/port/saturn/audio/saturn_pcm_transport.c"
#undef SM64_SATURN_PCM_STATUS_GENERATION_READ_OBSERVER

static void publish_v2_header(uint8_t *ram)
{
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_VERSION);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET,
                             SM64_SATURN_PCM_ABI_FLAG_COMPLETION);
}

static void publish_completion(uint8_t *ram, uint16_t producer,
                               uint16_t source_ring, uint16_t ticket_cursor,
                               uint16_t opcode, uint16_t status,
                               uint32_t generation, uint16_t detail,
                               uint16_t service_tick)
{
    const uint16_t base = (uint16_t)(
        SM64_SATURN_PCM_COMPLETION_RING_OFFSET +
        sm64_saturn_pcm_ring_cursor_slot(
            producer, SM64_SATURN_PCM_COMPLETION_RING_COUNT) *
            SM64_SATURN_PCM_COMPLETION_BYTES);
    sm64_saturn_pcm_put_be16(ram, base, source_ring);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 2U), ticket_cursor);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 4U), opcode);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 6U), status);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 8U),
                             (uint16_t)(generation >> 16));
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 10U),
                             (uint16_t)generation);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 12U), detail);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 14U), service_tick);
    sm64_saturn_pcm_put_be16(
        ram, SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET,
        sm64_saturn_pcm_ring_cursor_next(
            producer, SM64_SATURN_PCM_COMPLETION_RING_COUNT));
}

static void test_ticket_correlation_and_semantic_statuses(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    const uint16_t words[7] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_ticket_t ticket;
    sm64_saturn_audio_completion_t completion;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_audio_control_enqueue_ticket(
        &transport, SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE, words, &ticket));
    assert(ticket.source_ring == SM64_SATURN_AUDIO_RING_CONTROL);
    assert(ticket.cursor == 0U);
    assert(ticket.opcode == SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE);

    publish_completion(ram, 0U, SM64_SATURN_AUDIO_RING_CONTROL, 0U,
                       SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE,
                       SM64_SATURN_AUDIO_COMPLETION_PREPARED, 0x12345678U,
                       0x55AAU, 9U);
    assert(sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(sm64_saturn_audio_completion_matches_ticket(&completion, &ticket));
    assert(completion.status == SM64_SATURN_AUDIO_COMPLETION_PREPARED);
    assert(completion.generation == 0x12345678U);
    assert(completion.detail == 0x55AAU);
    assert(completion.service_tick == 9U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET) == 1U);

    completion.ticket.source_ring = SM64_SATURN_AUDIO_RING_SFX;
    assert(!sm64_saturn_audio_completion_matches_ticket(&completion, &ticket));
    completion.ticket = ticket;
    completion.ticket.cursor = 8U;
    assert(!sm64_saturn_audio_completion_matches_ticket(&completion, &ticket));
    completion.ticket = ticket;
    completion.ticket.opcode = SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT;
    assert(!sm64_saturn_audio_completion_matches_ticket(&completion, &ticket));

    completion.ticket = ticket;
    completion.status = SM64_SATURN_AUDIO_COMPLETION_ACCEPTED;
    assert(sm64_saturn_audio_completion_is_acceptance(&completion));
    completion.status = SM64_SATURN_AUDIO_COMPLETION_COMMITTED;
    assert(sm64_saturn_audio_completion_is_acceptance(&completion));
    completion.status = SM64_SATURN_AUDIO_COMPLETION_FINISHED;
    assert(!sm64_saturn_audio_completion_is_acceptance(&completion));
}

static void test_completion_validation_fails_closed(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_completion_t completion;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);

    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET, 64U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(transport.completion_protocol_faults == 1U);

    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET, 33U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET, 0U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(transport.completion_protocol_faults == 2U);

    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET, 0U);
    publish_completion(ram, 0U, 2U, 0U,
                       SM64_SATURN_AUDIO_OPCODE_RESET,
                       SM64_SATURN_AUDIO_COMPLETION_ACCEPTED, 1U, 0U, 0U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));

    publish_completion(ram, 0U, SM64_SATURN_AUDIO_RING_CONTROL, 0U,
                       SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH,
                       SM64_SATURN_AUDIO_COMPLETION_ACCEPTED, 1U, 0U, 0U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));

    publish_completion(ram, 0U, SM64_SATURN_AUDIO_RING_CONTROL, 0U,
                       SM64_SATURN_AUDIO_OPCODE_RESET, 0U, 1U, 0U, 0U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));

    publish_completion(ram, 0U, SM64_SATURN_AUDIO_RING_CONTROL, 0U,
                       SM64_SATURN_AUDIO_OPCODE_RESET,
                       SM64_SATURN_AUDIO_COMPLETION_ACCEPTED, 0U, 0U, 0U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET) == 0U);
    assert(transport.completion_protocol_faults == 6U);
}

static void test_status_snapshot_and_required_ack_classification(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_status_snapshot_t status;

    publish_v2_header(ram);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_ACTIVE_GENERATION_HIGH_OFFSET, 0x89ABU);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_ACTIVE_GENERATION_LOW_OFFSET, 0xCDEFU);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_PREPARED_GENERATION_HIGH_OFFSET, 0x1234U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_PREPARED_GENERATION_LOW_OFFSET, 0x5678U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_SATURATED_OFFSET, 7U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_COMPLETION_PROTOCOL_FAULTS_OFFSET, 8U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_LAST_COMPLETION_STATUS_OFFSET,
        SM64_SATURN_AUDIO_COMPLETION_COMMITTED);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_LAST_COMPLETION_DETAIL_OFFSET, 9U);
    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_audio_status_snapshot(&transport, &status));
    assert(status.active_generation == 0x89ABCDEFU);
    assert(status.prepared_generation == 0x12345678U);
    assert(status.completion_saturated == 7U);
    assert(status.completion_protocol_faults == 8U);
    assert(status.last_completion_status ==
           SM64_SATURN_AUDIO_COMPLETION_COMMITTED);
    assert(status.last_completion_detail == 9U);

    assert(sm64_saturn_audio_completion_is_required(
        SM64_SATURN_AUDIO_RING_CONTROL,
        SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT,
        SM64_SATURN_AUDIO_COMPLETION_COMMITTED));
    assert(!sm64_saturn_audio_completion_is_required(
        SM64_SATURN_AUDIO_RING_SFX,
        SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH,
        SM64_SATURN_AUDIO_COMPLETION_FINISHED));
    assert(!sm64_saturn_audio_completion_is_required(
        SM64_SATURN_AUDIO_RING_SFX,
        SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH,
        SM64_SATURN_AUDIO_COMPLETION_ACCEPTED));
}

static void test_capability_seqlock_and_play_refresh_rejection(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint8_t before[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    const uint16_t words[7] = {1U, 2U, 3U, 4U, 5U, 6U, 7U};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_ticket_t ticket;
    sm64_saturn_audio_status_snapshot_t status;

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_VERSION);
    sm64_saturn_pcm_transport_init(&transport, ram);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_control_enqueue_ticket(
        &transport, SM64_SATURN_AUDIO_OPCODE_RESET, words, &ticket));
    assert(memcmp(before, ram, sizeof(ram)) == 0);

    publish_v2_header(ram);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_play_refresh_enqueue_ticket(
        &transport, words, 0U, &ticket));
    assert(memcmp(before, ram, sizeof(ram)) == 0);
    assert(!sm64_saturn_audio_play_refresh_enqueue_ticket(
        &transport, words, 0x10000U, &ticket));
    assert(memcmp(before, ram, sizeof(ram)) == 0);
    assert(sm64_saturn_audio_play_refresh_enqueue_ticket(
        &transport, words, 0xFFFFU, &ticket));
    assert(sm64_saturn_pcm_get_be16(
               ram, (uint16_t)(SM64_SATURN_PCM_SFX_RING_OFFSET + 8U)) ==
           0xFFFFU);

    sm64_saturn_pcm_put_be16(
        ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET,
        (uint16_t)(SM64_SATURN_PCM_ABI_FLAG_COMPLETION |
                   SM64_SATURN_PCM_ABI_FLAG_STATUS_WRITING));
    assert(!sm64_saturn_audio_status_snapshot(&transport, &status));
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_ABI_FLAGS_OFFSET, 0U);
    assert(!sm64_saturn_audio_status_snapshot(&transport, &status));
}

static void test_pending_cursor_refuses_aba_until_completion(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    const uint16_t words[7] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_ticket_t tickets[16];
    sm64_saturn_audio_ticket_t retry;
    sm64_saturn_audio_completion_t completion;
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    for (i = 0U; i < 16U; ++i) {
        assert(sm64_saturn_audio_control_enqueue_ticket(
            &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words, &tickets[i]));
        sm64_saturn_pcm_put_be16(
            ram, SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET,
            sm64_saturn_pcm_ring_cursor_next(
                tickets[i].cursor, SM64_SATURN_PCM_CONTROL_RING_COUNT));
    }
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words));
    assert(transport.ticket_busy == 1U);

    publish_completion(ram, 0U, SM64_SATURN_AUDIO_RING_CONTROL,
                       tickets[0].cursor, tickets[0].opcode,
                       SM64_SATURN_AUDIO_COMPLETION_ACCEPTED, 1U, 0U, 1U);
    assert(sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(sm64_saturn_audio_control_enqueue_ticket(
        &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words, &retry));
    assert(retry.cursor == 0U);
}

static void test_wrong_same_class_opcode_does_not_retire_ticket(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    const uint16_t words[7] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_ticket_t ticket;
    sm64_saturn_audio_completion_t completion;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_audio_control_enqueue_ticket(
        &transport, SM64_SATURN_AUDIO_OPCODE_SEQ_START, words, &ticket));
    publish_completion(ram, 0U, ticket.source_ring, ticket.cursor,
                       SM64_SATURN_AUDIO_OPCODE_MUTE,
                       SM64_SATURN_AUDIO_COMPLETION_ACCEPTED, 1U, 0U, 1U);
    assert(!sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET) == 0U);
    assert(transport.control_ticket_pending[ticket.cursor] == ticket.opcode);

    publish_completion(ram, 0U, ticket.source_ring, ticket.cursor,
                       ticket.opcode, SM64_SATURN_AUDIO_COMPLETION_ACCEPTED,
                       1U, 0U, 2U);
    assert(sm64_saturn_audio_completion_poll(&transport, &completion));
    assert(transport.control_ticket_pending[ticket.cursor] == 0U);
}

static void test_status_sequence_retries_instead_of_accepting_torn_value(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_audio_status_snapshot_t status;

    publish_v2_header(ram);
    sm64_saturn_pcm_put_be16(
        ram, SM64_SATURN_PCM_ACTIVE_GENERATION_HIGH_OFFSET, 0x1111U);
    sm64_saturn_pcm_put_be16(
        ram, SM64_SATURN_PCM_ACTIVE_GENERATION_LOW_OFFSET, 0x2222U);
    sm64_saturn_pcm_transport_init(&transport, ram);
    s_status_read_observations = 0U;
    s_replace_status_during_read = true;
    assert(sm64_saturn_audio_status_snapshot(&transport, &status));
    assert(status.active_generation == 0x33334444U);
    assert(status.active_generation != 0x11114444U);
    assert(s_status_read_observations >= 2U);
}

int main(void)
{
    test_ticket_correlation_and_semantic_statuses();
    test_completion_validation_fails_closed();
    test_status_snapshot_and_required_ack_classification();
    test_capability_seqlock_and_play_refresh_rejection();
    test_pending_cursor_refuses_aba_until_completion();
    test_wrong_same_class_opcode_does_not_retire_ticket();
    test_status_sequence_retries_instead_of_accepting_torn_value();
    return 0;
}
