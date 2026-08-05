#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "pcm_voice.h"
#include "scsp_pcm8.h"
#include "saturn_pcm_protocol.h"

static void publish_v2_header(uint8_t *ram)
{
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_VERSION);
}

static void put_ring_command(uint8_t *ram, uint16_t ring_offset,
                             uint16_t ring_count, uint16_t cursor,
                             uint16_t opcode, const uint16_t words[7])
{
    uint16_t i;
    const uint16_t base = (uint16_t)(ring_offset +
        sm64_saturn_pcm_ring_cursor_slot(cursor, ring_count) *
            SM64_SATURN_PCM_COMMAND_BYTES);
    sm64_saturn_pcm_put_be16(ram, base, opcode);
    for (i = 0U; i < 7U; ++i) {
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 2U + i * 2U),
                                words[i]);
    }
}

static void put_control(uint8_t *ram, uint16_t cursor, uint16_t opcode,
                        const uint16_t words[7])
{
    put_ring_command(ram, SM64_SATURN_PCM_CONTROL_RING_OFFSET,
                     SM64_SATURN_PCM_CONTROL_RING_COUNT, cursor, opcode,
                     words);
}

static void put_sfx(uint8_t *ram, uint16_t cursor, uint16_t opcode,
                    const uint16_t words[7])
{
    put_ring_command(ram, SM64_SATURN_PCM_SFX_RING_OFFSET,
                     SM64_SATURN_PCM_SFX_RING_COUNT, cursor, opcode, words);
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

static void test_consumer_drains_control_before_sfx_under_budget(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0U, 12U, (uint16_t)-16, 0U, 0U, 0U, 0U};
    const uint16_t master[7] = {9U, 0U, 0U, 0U, 0U, 0U, 0U};
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_voice_state_init(&state);
    for (i = 0U; i < 8U; ++i) {
        put_sfx(ram, i, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    }
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SET_MASTER, master);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 8U);

    assert(sm64_saturn_pcm68k_consume(ram, &state) == 8U);
    assert(state.master_volume == 9U);
    assert(state.control_commands_consumed == 1U);
    assert(state.sfx_commands_consumed == 7U);
    assert(state.voices_started == 7U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_CONSUMER_OFFSET) == 7U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_CONSUMED_OFFSET) == 7U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 1U);
}

static void test_reset_play_invalid_and_unknown_are_safe(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t valid_play[7] = {0U, 15U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t invalid_play[7] = {99U, 15U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t zero[7] = {0};

    publish_v2_header(ram);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, 0x00FEU, zero);
    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_RESET, zero);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, valid_play);
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, invalid_play);
    put_sfx(ram, 2U, 0x01FEU, zero);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 3U);

    assert(sm64_saturn_pcm68k_consume(ram, &state) == 5U);
    assert(state.invalid_samples == 1U);
    assert(state.unknown_opcodes == 2U);
    assert(state.commands_consumed == 5U);
    assert(state.last_opcode == 0x01FEU);
    assert(state.voices_started == 1U);
}

static void test_version_and_either_corrupt_ring_fail_before_consumption(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t reset[7] = {0};

    sm64_saturn_pcm_voice_state_init(&state);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_VERSION_V1);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 0U);
    assert(state.protocol_faults == 1U);

    publish_v2_header(ram);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_RESET, reset);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 48U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 0U);
    assert(state.protocol_faults == 2U);
    assert(state.commands_consumed == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET) == 0U);

    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 9U);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 0U);
    assert(state.protocol_faults == 3U);
    assert(state.commands_consumed == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET) == 3U);
}

static uint16_t register_word(const uint16_t *registers, uint16_t offset)
{
    return registers[offset / 2U];
}

static void test_consumer_drives_scsp_play_master_and_reset(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0U, 12U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t master[7] = {9U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t reset[7] = {0};

    publish_v2_header(ram);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SET_MASTER, master);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 2U);
    assert(register_word(register_words, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1810U);
    assert(register_word(register_words, SM64_SATURN_SCSP_MASTER_OFFSET) ==
           0x0209U);

    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_RESET, reset);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(register_word(register_words, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1000U);
}

int main(void)
{
    test_proof_metadata_is_deterministic_and_bounded();
    test_consumer_drains_control_before_sfx_under_budget();
    test_reset_play_invalid_and_unknown_are_safe();
    test_version_and_either_corrupt_ring_fail_before_consumption();
    test_consumer_drives_scsp_play_master_and_reset();
    return 0;
}
