#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_pcm_protocol.h"
#include "saturn_pcm_transport.h"

static void test_enqueue_encodes_big_endian_and_tracks_occupancy(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {2U, 0x1234U, 0xFF80U, 7U, 8U, 9U, 10U};
    uint16_t i;

    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_pcm_enqueue(&transport,
                                   SM64_SATURN_PCM_OPCODE_PLAY, words));
    assert(sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_RING_OFFSET) ==
           SM64_SATURN_PCM_OPCODE_PLAY);
    for (i = 0; i < 7U; ++i) {
        assert(sm64_saturn_pcm_get_be16(
                   ram, (uint16_t)(SM64_SATURN_PCM_RING_OFFSET + 2U + i * 2U)) ==
               words[i]);
    }
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_PRODUCER_OFFSET) == 1U);
    assert(transport.enqueued == 1U);
    assert(transport.dropped == 0U);
    assert(transport.high_water == 1U);
}

static void test_ring_wrap_full_and_drop_are_bounded(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {0};
    uint16_t i;

    sm64_saturn_pcm_transport_init(&transport, ram);
    for (i = 0; i < SM64_SATURN_PCM_RING_COUNT - 1U; ++i) {
        assert(sm64_saturn_pcm_enqueue(&transport,
                                       SM64_SATURN_PCM_OPCODE_STOP_ALL,
                                       words));
    }
    assert(transport.high_water == SM64_SATURN_PCM_RING_COUNT - 1U);
    assert(!sm64_saturn_pcm_enqueue(&transport,
                                    SM64_SATURN_PCM_OPCODE_STOP_ALL, words));
    assert(transport.dropped == 1U);
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_PRODUCER_OFFSET) == 31U);

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_CONSUMER_OFFSET, 30U);
    assert(sm64_saturn_pcm_enqueue(&transport,
                                   SM64_SATURN_PCM_OPCODE_STOP_ALL, words));
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_PRODUCER_OFFSET) == 0U);
}

static void test_invalid_inputs_fail_closed(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    uint8_t before[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {0};

    memset(ram, 0xA5, sizeof(ram));
    memcpy(before, ram, sizeof(ram));
    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(!sm64_saturn_pcm_enqueue(&transport,
                                    (sm64_saturn_pcm_opcode_t)99, words));
    assert(memcmp(ram, before, sizeof(ram)) == 0);
    assert(transport.dropped == 1U);
    assert(!sm64_saturn_pcm_enqueue(NULL,
                                    SM64_SATURN_PCM_OPCODE_STOP_ALL, words));
    transport.sound_ram = NULL;
    assert(!sm64_saturn_pcm_enqueue(&transport,
                                    SM64_SATURN_PCM_OPCODE_STOP_ALL, words));
    assert(transport.dropped == 2U);
}

int main(void)
{
    test_enqueue_encodes_big_endian_and_tracks_occupancy();
    test_ring_wrap_full_and_drop_are_bounded();
    test_invalid_inputs_fail_closed();
    return 0;
}
