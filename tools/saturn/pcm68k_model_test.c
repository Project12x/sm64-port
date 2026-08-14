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

static void publish_sfx_bundle(uint8_t *ram, uint32_t sound_bits,
                               uint16_t generation, uint32_t sample_offset,
                               uint16_t sample_count, uint16_t sample_rate)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint16_t maps = (uint16_t)(base + SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    const uint16_t samples = (uint16_t)(maps + SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
    sm64_saturn_pcm_put_be32(ram, base, SM64_SATURN_PCM_SFX_BUNDLE_MAGIC);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 4U),
                             SM64_SATURN_PCM_SFX_BUNDLE_VERSION);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 6U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 8U), generation);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 10U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 12U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 14U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 16U),
                             (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES +
                                        SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES));
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 18U),
                             (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES +
                                        SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES +
                                        SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES));
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 20U), sample_count);
    sm64_saturn_pcm_put_be32(ram, maps, sound_bits);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(maps + 4U), 0U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(maps + 6U), 1U);
    sm64_saturn_pcm_put_be32(ram, samples, sample_offset);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 4U), sample_count);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 6U), sample_rate);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 8U), 15U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 10U), 0U);
}

static void test_semantic_sequence_start_reaches_scsp(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 3U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t sequence[] = {0x90U, 0x00U, 0x03U, 0x41U, 0xc0U, 1U, 0xffU};
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint16_t sample = (uint16_t)(base + 40U);
    const uint32_t sequence_offset = SM64_SATURN_PCM_BANK_OFFSET + 32U;
    uint16_t i;

    publish_v2_header(ram);
    sm64_saturn_pcm_voice_state_init(&state);
    sm64_saturn_pcm_put_be32(ram, base, SM64_SATURN_PCM_SFX_BUNDLE_MAGIC);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 4U),
                             SM64_SATURN_PCM_SFX_BUNDLE_VERSION);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 6U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 8U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 10U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 12U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 14U), 32U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 16U), 40U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 18U), 52U);
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 20U),
                             sizeof(sequence) + 32U);
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 24U), sequence_offset);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 28U), sizeof(sequence));
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 30U), 0U);
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 32U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 36U), 0U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 38U), 1U);
    sm64_saturn_pcm_put_be32(ram, sample, SM64_SATURN_PCM_BANK_OFFSET);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(sample + 4U), 32U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(sample + 6U), 11025U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(sample + 8U), 15U);
    for (i = 0U; i < 32U; ++i) ram[SM64_SATURN_PCM_BANK_OFFSET + i] = 0x80U;
    for (i = 0U; i < sizeof(sequence); ++i) {
        ram[sequence_offset + i] = sequence[i];
    }
    {
        sm64_saturn_pcm_sample_t check;
        assert(sm64_saturn_pcm_sfx_sample_descriptor(ram, 0U, &check));
    }
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    for (i = 0U; i < 8192U && state.voices_started == 0U; ++i) {
        (void)sm64_saturn_pcm68k_consume_scsp(ram, registers, &state);
    }
    assert(state.music_sequence_starts == 1U);
    assert(state.music_notes_started != 0U);
    assert(state.voices_started != 0U);
    assert(state.music_faults == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET) != 0U);
    assert(state.music_direct_fallback != 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET) != 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_STARTS_OFFSET) != 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_VM_TICKS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET) != 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_NOTES_OFFSET) != 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_REJECT_MASK_OFFSET) == 0U);
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

static void test_semantic_sfx_uses_validated_sound_ram_bundle(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0x2400U, 0x8080U, 9U, 1U, 0xFF40U, 4096U, 1U};

    publish_v2_header(ram);
    publish_sfx_bundle(ram, 0x24008080U, 1U,
                       SM64_SATURN_PCM_BANK_OFFSET, 32U, 16000U);
    sm64_saturn_pcm_voice_state_init(&state);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.invalid_samples == 0U);
    assert(register_word(register_words, SM64_SATURN_SCSP_SLOT_SA_LOW) ==
           SM64_SATURN_PCM_BANK_OFFSET);

    /* A malformed present bundle must fail closed instead of falling back to
     * the historic proof-tone table. */
    sm64_saturn_pcm_put_be32(ram,
        (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_OFFSET +
                   SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES +
                   SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES),
        SM64_SATURN_PCM_SOUND_RAM_BYTES - 8U);
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.invalid_samples == 1U);
}

int main(void)
{
    test_proof_metadata_is_deterministic_and_bounded();
    test_semantic_sequence_start_reaches_scsp();
    test_consumer_drains_control_before_sfx_under_budget();
    test_reset_play_invalid_and_unknown_are_safe();
    test_version_and_either_corrupt_ring_fail_before_consumption();
    test_consumer_drives_scsp_play_master_and_reset();
    test_semantic_sfx_uses_validated_sound_ram_bundle();
    return 0;
}
