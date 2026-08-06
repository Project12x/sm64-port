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
    assert(SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET == 0x4026U);
    assert(SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET == 0x4028U);
    assert(SM64_SATURN_PCM_ABI_FLAGS_OFFSET + 2U <= 0x4040U);
    assert(SM64_SATURN_PCM_COMPLETION_RING_OFFSET == 0x4240U);
    assert(SM64_SATURN_PCM_COMPLETION_RING_COUNT == 32U);
    assert(SM64_SATURN_PCM_COMPLETION_BYTES == 16U);
    assert(SM64_SATURN_PCM_COMPLETION_RING_BYTES == 512U);
    assert(SM64_SATURN_PCM_COMPLETION_RING_OFFSET +
               SM64_SATURN_PCM_COMPLETION_RING_BYTES == 0x4440U);
    assert(SM64_SATURN_PCM_COMMAND_BYTES == 16U);

    assert(sm64_saturn_pcm_ring_cursor_next(15U, 8U) == 0U);
    assert(sm64_saturn_pcm_ring_cursor_slot(9U, 8U) == 1U);
    assert(sm64_saturn_pcm_ring_occupancy(8U, 0U, 8U) == 8U);
    assert(sm64_saturn_pcm_ring_occupancy(0U, 15U, 8U) == 1U);
    assert(sm64_saturn_pcm_ring_cursor_is_valid(15U, 8U));
    assert(!sm64_saturn_pcm_ring_cursor_is_valid(16U, 8U));
    assert(sm64_saturn_pcm_ring_cursor_next(63U, 32U) == 0U);
    assert(sm64_saturn_pcm_ring_cursor_slot(47U, 32U) == 15U);
    assert(sm64_saturn_pcm_ring_occupancy(32U, 0U, 32U) == 32U);
}

static void test_completion_status_and_generation_contract(void)
{
    uint16_t epoch = 0U;

    assert(SM64_SATURN_AUDIO_COMPLETION_ACCEPTED == 1U);
    assert(SM64_SATURN_AUDIO_COMPLETION_REJECTED == 2U);
    assert(SM64_SATURN_AUDIO_COMPLETION_STALE == 3U);
    assert(SM64_SATURN_AUDIO_COMPLETION_FAULT == 4U);
    assert(SM64_SATURN_AUDIO_COMPLETION_DROPPED_SFX == 5U);
    assert(SM64_SATURN_AUDIO_COMPLETION_FINISHED == 6U);
    assert(SM64_SATURN_AUDIO_COMPLETION_COMMITTED == 7U);
    assert(SM64_SATURN_AUDIO_COMPLETION_PREPARED == 8U);
    assert(sm64_saturn_audio_completion_status_is_valid(
        SM64_SATURN_AUDIO_COMPLETION_PREPARED));
    assert(!sm64_saturn_audio_completion_status_is_valid(0U));
    assert(!sm64_saturn_audio_completion_status_is_valid(9U));

    assert(sm64_saturn_audio_generation_advances(1U, 2U));
    assert(!sm64_saturn_audio_generation_advances(0U, 1U));
    assert(!sm64_saturn_audio_generation_advances(1U, 0U));
    assert(!sm64_saturn_audio_generation_advances(2U, 2U));
    assert(!sm64_saturn_audio_generation_advances(2U, 1U));
    assert(!sm64_saturn_audio_generation_advances(UINT32_MAX, 1U));

    assert(sm64_saturn_audio_play_refresh_epoch_from_generation(1U, &epoch));
    assert(epoch == 1U);
    assert(sm64_saturn_audio_play_refresh_epoch_from_generation(0xFFFFU,
                                                                 &epoch));
    assert(epoch == 0xFFFFU);
    assert(!sm64_saturn_audio_play_refresh_epoch_from_generation(0U, &epoch));
    assert(!sm64_saturn_audio_play_refresh_epoch_from_generation(0x10000U,
                                                                  &epoch));
    assert(!sm64_saturn_audio_play_refresh_epoch_from_generation(
        0x12345678U, &epoch));

    assert(sm64_saturn_pcm_counter_saturating_increment(0U) == 1U);
    assert(sm64_saturn_pcm_counter_saturating_increment(UINT16_MAX) ==
           UINT16_MAX);
    assert(sm64_saturn_audio_completion_publication_allowed(0U, false));
    assert(sm64_saturn_audio_completion_publication_allowed(23U, false));
    assert(!sm64_saturn_audio_completion_publication_allowed(24U, false));
    assert(sm64_saturn_audio_completion_publication_allowed(24U, true));
    assert(sm64_saturn_audio_completion_publication_allowed(31U, true));
    assert(!sm64_saturn_audio_completion_publication_allowed(32U, true));
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
    test_completion_status_and_generation_contract();
    return 0;
}
