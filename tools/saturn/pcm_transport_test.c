#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_pcm_protocol.h"
#include "saturn_pcm_transport.h"

static void publish_v2_header(uint8_t *ram)
{
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_VERSION);
}

static void test_control_and_sfx_encode_big_endian_in_disjoint_rings(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {2U, 0x1234U, 0xFF80U, 7U, 8U, 9U, 10U};
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_SEQ_START, words));
    assert(sm64_saturn_audio_sfx_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));

    assert(sm64_saturn_pcm_get_be16(ram,
        SM64_SATURN_PCM_CONTROL_RING_OFFSET) ==
        SM64_SATURN_AUDIO_OPCODE_SEQ_START);
    assert(sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_SFX_RING_OFFSET) ==
           SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);
    for (i = 0U; i < 7U; ++i) {
        assert(sm64_saturn_pcm_get_be16(
                   ram, (uint16_t)(SM64_SATURN_PCM_CONTROL_RING_OFFSET + 2U +
                                   i * 2U)) == words[i]);
        assert(sm64_saturn_pcm_get_be16(
                   ram, (uint16_t)(SM64_SATURN_PCM_SFX_RING_OFFSET + 2U +
                                   i * 2U)) == words[i]);
    }
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 1U);
    assert(transport.control_enqueued == 1U);
    assert(transport.sfx_enqueued == 1U);
    assert(transport.control_high_water == 1U);
    assert(transport.sfx_high_water == 1U);
}

static void test_sfx_saturation_cannot_consume_control_capacity(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {0};
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    for (i = 0U; i < SM64_SATURN_PCM_SFX_RING_COUNT; ++i) {
        assert(sm64_saturn_audio_sfx_enqueue(
            &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));
    }
    assert(!sm64_saturn_audio_sfx_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));
    assert(transport.sfx_saturated == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_SATURATED_OFFSET) == 1U);
    assert(sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_SEQ_STOP, words));
    assert(transport.control_saturated == 0U);

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_CONSUMER_OFFSET, 24U);
    for (i = 0U; i < SM64_SATURN_PCM_SFX_RING_COUNT; ++i) {
        assert(sm64_saturn_audio_sfx_enqueue(
            &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));
    }
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 0U);
}

static void test_control_full_capacity_wrap_and_saturation_telemetry(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {0};
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_transport_init(&transport, ram);
    for (i = 0U; i < SM64_SATURN_PCM_CONTROL_RING_COUNT; ++i) {
        assert(sm64_saturn_audio_control_enqueue(
            &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words));
    }
    assert(transport.control_high_water == SM64_SATURN_PCM_CONTROL_RING_COUNT);
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words));
    assert(transport.control_saturated == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET) == 1U);

    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET, 8U);
    for (i = 0U; i < SM64_SATURN_PCM_CONTROL_RING_COUNT; ++i) {
        assert(sm64_saturn_audio_control_enqueue(
            &transport, SM64_SATURN_AUDIO_OPCODE_MUTE, words));
    }
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 0U);
}

static void test_version_opcode_and_corrupt_cursor_fail_closed(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint8_t before[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    sm64_saturn_pcm_transport_t transport;
    const uint16_t words[7] = {0};

    sm64_saturn_pcm_transport_init(&transport, ram);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_RESET, words));
    assert(memcmp(ram, before, sizeof(ram)) == 0);

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_VERSION_V1);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_sfx_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));
    assert(memcmp(ram, before, sizeof(ram)) == 0);

    publish_v2_header(ram);
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words));
    assert(!sm64_saturn_audio_sfx_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_SEQ_START, words));
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 16U);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_RESET, words));
    assert(memcmp(ram, before, sizeof(ram)) == 0);

    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 9U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET, 0U);
    memcpy(before, ram, sizeof(ram));
    assert(!sm64_saturn_audio_control_enqueue(
        &transport, SM64_SATURN_AUDIO_OPCODE_RESET, words));
    assert(memcmp(ram, before, sizeof(ram)) == 0);
    assert(transport.protocol_faults == 6U);
}

int main(void)
{
    test_control_and_sfx_encode_big_endian_in_disjoint_rings();
    test_sfx_saturation_cannot_consume_control_capacity();
    test_control_full_capacity_wrap_and_saturation_telemetry();
    test_version_opcode_and_corrupt_cursor_fail_closed();
    return 0;
}
