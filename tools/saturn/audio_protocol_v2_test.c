/* Host contract for the version-two semantic-audio mailbox ABI. */
#include <assert.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

static void test_split_ring_layout_and_full_capacity_cursor_wrap(void)
{
    assert(SM64_SATURN_PCM_PROTOCOL_VERSION == 2U);
    assert(SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET == 0x4008U);
    assert(SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET == 0x400AU);
    assert(SM64_SATURN_PCM_SFX_PRODUCER_OFFSET == 0x400CU);
    assert(SM64_SATURN_PCM_SFX_CONSUMER_OFFSET == 0x400EU);
    assert(SM64_SATURN_PCM_CONTROL_RING_OFFSET == 0x4040U);
    assert(SM64_SATURN_PCM_CONTROL_RING_COUNT == 8U);
    assert(SM64_SATURN_PCM_CONTROL_RING_BYTES == 128U);
    assert(SM64_SATURN_PCM_SFX_RING_OFFSET == 0x40C0U);
    assert(SM64_SATURN_PCM_SFX_RING_COUNT == 24U);
    assert(SM64_SATURN_PCM_SFX_RING_BYTES == 384U);
    assert(SM64_SATURN_PCM_SFX_RING_OFFSET +
               SM64_SATURN_PCM_SFX_RING_BYTES == 0x4240U);
    assert(SM64_SATURN_PCM_COMMAND_BYTES == 16U);

    assert(sm64_saturn_pcm_ring_cursor_next(15U, 8U) == 0U);
    assert(sm64_saturn_pcm_ring_cursor_slot(9U, 8U) == 1U);
    assert(sm64_saturn_pcm_ring_occupancy(8U, 0U, 8U) == 8U);
    assert(sm64_saturn_pcm_ring_occupancy(0U, 15U, 8U) == 1U);
    assert(sm64_saturn_pcm_ring_cursor_is_valid(15U, 8U));
    assert(!sm64_saturn_pcm_ring_cursor_is_valid(16U, 8U));
}

static void test_opcode_classes_are_closed_and_disjoint(void)
{
    assert(sm64_saturn_audio_opcode_is_control(
        SM64_SATURN_AUDIO_OPCODE_RESET));
    assert(sm64_saturn_audio_opcode_is_control(
        SM64_SATURN_AUDIO_OPCODE_SOUND_MODE));
    assert(!sm64_saturn_audio_opcode_is_control(
        SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH));
    assert(sm64_saturn_audio_opcode_is_sfx(
        SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH));
    assert(sm64_saturn_audio_opcode_is_sfx(
        SM64_SATURN_AUDIO_OPCODE_STOP_BANK));
    assert(!sm64_saturn_audio_opcode_is_sfx(
        SM64_SATURN_AUDIO_OPCODE_SEQ_START));
    assert(!sm64_saturn_audio_opcode_is_control(
        (sm64_saturn_audio_opcode_t)0xFFFFU));
    assert(!sm64_saturn_audio_opcode_is_sfx(
        (sm64_saturn_audio_opcode_t)0xFFFFU));
}

int main(void)
{
    test_split_ring_layout_and_full_capacity_cursor_wrap();
    test_opcode_classes_are_closed_and_disjoint();
    return 0;
}
