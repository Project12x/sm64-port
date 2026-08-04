#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "pcm_voice.h"
#include "saturn_pcm_protocol.h"

static void put_command(uint8_t *ram, uint16_t index, uint16_t opcode,
                        const uint16_t words[7])
{
    uint16_t i;
    const uint16_t base = (uint16_t)(SM64_SATURN_PCM_RING_OFFSET +
                                    index * SM64_SATURN_PCM_COMMAND_BYTES);
    sm64_saturn_pcm_put_be16(ram, base, opcode);
    for (i = 0; i < 7U; ++i) {
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 2U + i * 2U),
                                words[i]);
    }
}

static void test_proof_metadata_is_deterministic_and_bounded(void)
{
    uint16_t i;
    assert(sm64_saturn_pcm_proof_sample_count() == 3U);
    for (i = 0; i < sm64_saturn_pcm_proof_sample_count(); ++i) {
        const sm64_saturn_pcm_sample_t *sample =
            sm64_saturn_pcm_proof_sample(i);
        assert(sample != NULL);
        assert((sample->sound_ram_offset & 1U) == 0U);
        assert(sample->sound_ram_offset >= SM64_SATURN_PCM_BANK_OFFSET);
        assert(sample->sample_count > 0U);
        assert(sample->sample_rate == 11025U);
        assert(sample->sound_ram_offset + sample->sample_count <=
               SM64_SATURN_PCM_BANK_OFFSET + 32768U);
    }
    assert(sm64_saturn_pcm_proof_sample(3U) == NULL);
}

static void test_consumer_plays_round_robin_and_reuses_with_keyoff(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0U, 12U, (uint16_t)-16, 0U, 0U, 0U, 0U};
    uint16_t i;

    sm64_saturn_pcm_voice_state_init(&state);
    for (i = 0; i < 5U; ++i) {
        put_command(ram, i, SM64_SATURN_PCM_OPCODE_PLAY, play);
    }
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, 5U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 5U);
    assert(state.voices_started == 5U);
    assert(state.voices[0].active);
    assert(state.voices[0].generation == 2U);
    assert(state.keyoffs == 1U);
    assert(state.next_slot == 1U);
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_CONSUMER_OFFSET) == 5U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET) == 5U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET) == 5U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET) == 0U);
}

static void test_master_stop_invalid_and_unknown_are_safe(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t master[7] = {99U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t valid_play[7] = {0U, 15U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t invalid_play[7] = {99U, 15U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t zero[7] = {0};

    sm64_saturn_pcm_voice_state_init(&state);
    put_command(ram, 0U, SM64_SATURN_PCM_OPCODE_SET_MASTER, master);
    put_command(ram, 1U, SM64_SATURN_PCM_OPCODE_PLAY, valid_play);
    put_command(ram, 2U, SM64_SATURN_PCM_OPCODE_PLAY, invalid_play);
    put_command(ram, 3U, 99U, zero);
    put_command(ram, 4U, SM64_SATURN_PCM_OPCODE_STOP_ALL, zero);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, 5U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 5U);
    assert(state.master_volume == 15U);
    assert(state.invalid_samples == 1U);
    assert(state.unknown_opcodes == 1U);
    assert(state.commands_consumed == 5U);
    assert(state.last_opcode == SM64_SATURN_PCM_OPCODE_STOP_ALL);
    assert(state.voices_started == 1U);
    assert(!state.voices[0].active);
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET) ==
           0xFFFFU);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_LAST_OPCODE_OFFSET) ==
           SM64_SATURN_PCM_OPCODE_STOP_ALL);
}

static void test_consumer_caps_each_poll_and_rejects_corrupt_indices(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t stop[7] = {0};
    uint16_t i;

    sm64_saturn_pcm_voice_state_init(&state);
    for (i = 0; i < 12U; ++i) {
        put_command(ram, i, SM64_SATURN_PCM_OPCODE_STOP_ALL, stop);
    }
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, 12U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 8U);
    assert(sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_CONSUMER_OFFSET) == 8U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 4U);

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, 32U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 0U);
    assert(state.protocol_faults == 1U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_PRODUCER_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_CONSUMER_OFFSET, 32U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 0U);
    assert(state.protocol_faults == 2U);
}

int main(void)
{
    test_proof_metadata_is_deterministic_and_bounded();
    test_consumer_plays_round_robin_and_reuses_with_keyoff();
    test_master_stop_invalid_and_unknown_are_safe();
    test_consumer_caps_each_poll_and_rejects_corrupt_indices();
    return 0;
}
