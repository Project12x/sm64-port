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

/* Task 6 no-music discrimination: this attested bundle carries the retired
 * m64 trailer words but a zero music_sample_index, so SEQ_START must key any
 * stale music off (replace semantics), start nothing, and count no fault.
 * The command must stay a known opcode, the bundle must keep parsing, and
 * the VM-sourced mailbox words must publish 0 without moving any offset. */
static void test_seq_start_is_consumed_and_keys_music_off(void)
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
    /* Pre-set the activity flag so the key-off assertion below discriminates
     * "SEQ_START keyed music off" from "SEQ_START did nothing": state_init
     * already leaves music_active at 0. */
    state.music_active = 1U;
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.unknown_opcodes == 0U);
    assert(state.protocol_faults == 0U);
    assert(state.music_active == 0U);
    assert(state.music_sequence_starts == 0U);
    assert(state.music_notes_started == 0U);
    assert(state.music_faults == 0U);
    assert(state.voices_started == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_STARTS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_VM_TICKS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_NOTES_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_REJECT_MASK_OFFSET) == 0U);
    /* The requested source sequence id (words[1]) is recorded in the
     * MUSIC_SEQUENCE diagnostic word even when no music row exists. */
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_SEQUENCE_OFFSET) == 3U);
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
    /* The budget contract is about commands consumed, not voices: these are
     * seven refreshes of the SAME proof sample, so since the R1 fix one voice
     * is keyed on and the other six refresh it in place. */
    assert(state.voices_started == 1U);
    assert(state.sfx_refreshes == 6U);
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

static uint16_t slot_word(const uint16_t *registers, uint16_t slot,
                          uint16_t field)
{
    return register_word(registers,
                         (uint16_t)(slot * SM64_SATURN_SCSP_SLOT_BYTES +
                                    field));
}

/* Task 6: bundle with one semantic-SFX row plus a trailer-indexed music row.
 * The trailer index and the music row's flags word are caller-controlled so
 * tests cover the valid looped row and the loop-less invalid row. */
static void publish_music_bundle(uint8_t *ram, uint16_t music_index,
                                 uint16_t music_flags)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint16_t maps = (uint16_t)(base +
        SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    const uint16_t samples = (uint16_t)(maps +
        SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
    const uint16_t music_row = (uint16_t)(samples +
        SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
    sm64_saturn_pcm_put_be32(ram, base, SM64_SATURN_PCM_SFX_BUNDLE_MAGIC);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 4U),
                             SM64_SATURN_PCM_SFX_BUNDLE_VERSION);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 6U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 8U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 10U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 12U), 2U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 14U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 16U),
                             (uint16_t)(samples - base));
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 18U),
                             (uint16_t)(samples - base +
                                        2U * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES));
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 20U), 96U);
    sm64_saturn_pcm_put_be16(ram,
        (uint16_t)(base + SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SAMPLE_INDEX_FIELD),
        music_index);
    sm64_saturn_pcm_put_be32(ram, maps, 0x24008080U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(maps + 4U), 0U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(maps + 6U), 1U);
    /* Row 0: the mapped SFX sample. */
    sm64_saturn_pcm_put_be32(ram, samples, SM64_SATURN_PCM_BANK_OFFSET);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 4U), 32U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 6U), 16000U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 8U), 15U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(samples + 10U), 0U);
    /* Row 1: the music sample the trailer points at. */
    sm64_saturn_pcm_put_be32(ram, music_row,
                             SM64_SATURN_PCM_BANK_OFFSET + 32U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 4U), 64U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 6U), 11025U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 8U), 12U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 10U), music_flags);
}

/* Bundle carrying `sound_count` DISTINCT semantic-SFX mappings -- sound_bits
 * 0x24008080 + i naming sample row i -- optionally followed by a looped music
 * row at index `sound_count` that the trailer points at.  Every SFX row names
 * the same PCM bytes, so only the sample id (the row index) distinguishes the
 * sounds: that is exactly the key the already-playing check compares. */
static void publish_distinct_sound_bundle(uint8_t *ram, uint16_t sound_count,
                                          bool with_music)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint16_t sample_offset = (uint16_t)(
        SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES +
        sound_count * SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
    const uint16_t sample_count = (uint16_t)(sound_count +
                                             (with_music ? 1U : 0U));
    const uint16_t maps = (uint16_t)(base +
        SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    const uint16_t samples = (uint16_t)(base + sample_offset);
    uint16_t i;
    sm64_saturn_pcm_put_be32(ram, base, SM64_SATURN_PCM_SFX_BUNDLE_MAGIC);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 4U),
                             SM64_SATURN_PCM_SFX_BUNDLE_VERSION);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 6U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 8U), 1U);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 10U), sound_count);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 12U), sample_count);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 14U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 16U), sample_offset);
    sm64_saturn_pcm_put_be16(ram, (uint16_t)(base + 18U),
        (uint16_t)(sample_offset +
                   sample_count * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES));
    sm64_saturn_pcm_put_be32(ram, (uint16_t)(base + 20U), 96U);
    sm64_saturn_pcm_put_be16(ram,
        (uint16_t)(base + SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SAMPLE_INDEX_FIELD),
        with_music ? sound_count : 0U);
    for (i = 0U; i < sound_count; ++i) {
        const uint16_t mapping = (uint16_t)(maps +
            i * SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
        const uint16_t row = (uint16_t)(samples +
            i * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
        sm64_saturn_pcm_put_be32(ram, mapping, 0x24008080U + i);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(mapping + 4U), i);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(mapping + 6U), 1U);
        sm64_saturn_pcm_put_be32(ram, row, SM64_SATURN_PCM_BANK_OFFSET);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(row + 4U), 32U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(row + 6U), 16000U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(row + 8U), 15U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(row + 10U), 0U);
    }
    if (with_music) {
        const uint16_t music_row = (uint16_t)(samples +
            sound_count * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
        sm64_saturn_pcm_put_be32(ram, music_row,
                                 SM64_SATURN_PCM_BANK_OFFSET + 32U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 4U), 64U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 6U), 11025U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 8U), 12U);
        sm64_saturn_pcm_put_be16(ram, (uint16_t)(music_row + 10U),
                                 SM64_SATURN_PCM_SAMPLE_LOOP);
    }
}

/* R1 defect regression.  SM64 re-asserts a continuous sound on every
 * game-loop tick; the driver used to run the full key-off/reprogram/key-on
 * sequence each time, restarting the sample from offset 0 -- at the shipped
 * frame rate, an audible periodic burst instead of the sound.  A repeat of
 * the same PLAY_REFRESH must instead refresh the slot in place: KEY_ON is
 * written exactly once across both refreshes, and the voice keeps its slot. */
static void test_repeated_play_refresh_updates_without_rekey(void)
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
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.sfx_refreshes == 0U);
    assert(state.voices[1].active);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1810U);

    /* Clearing the shadow makes the second refresh's register traffic
     * visible: any key-on would rewrite KEYS, any restart would rewrite the
     * sample address.  Both must stay clear. */
    register_words[(1U * SM64_SATURN_SCSP_SLOT_BYTES +
                    SM64_SATURN_SCSP_SLOT_KEYS) / 2U] = 0U;
    register_words[(1U * SM64_SATURN_SCSP_SLOT_BYTES +
                    SM64_SATURN_SCSP_SLOT_SA_LOW) / 2U] = 0U;
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_SA_LOW) == 0U);
    /* Nor did the refresh key the sound on somewhere else: KEY_ON was
     * written exactly once across both refreshes, on slot 1 only. */
    assert(slot_word(register_words, 2U, SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    assert(slot_word(register_words, 3U, SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    /* One key-on across both refreshes, and no second voice anywhere. */
    assert(state.voices_started == 1U);
    assert(state.sfx_refreshes == 1U);
    assert(state.keyoffs == 0U);
    assert(state.invalid_samples == 0U);
    assert(state.voices[1].active);
    assert(!state.voices[2].active);
    assert(!state.voices[3].active);
    assert(state.next_slot == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_SFX_REFRESHES_OFFSET) == 1U);
}

/* Holding a sound must still track its level: the refresh path writes the
 * attenuation and pan/send words -- and only those -- with the new values. */
static void test_repeated_play_refresh_still_applies_volume_and_pan(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    /* words[4] 0xFF40: volume 15 (send level 7), pan 0. */
    const uint16_t loud[7] = {0x2400U, 0x8080U, 9U, 1U, 0xFF40U, 4096U, 1U};
    /* words[4] 0x8020: volume 8 (send level 4), pan -15 (pan word 0x18). */
    const uint16_t quiet[7] = {0x2400U, 0x8080U, 9U, 1U, 0x8020U, 4096U, 2U};

    publish_v2_header(ram);
    publish_sfx_bundle(ram, 0x24008080U, 1U,
                       SM64_SATURN_PCM_BANK_OFFSET, 32U, 16000U);
    sm64_saturn_pcm_voice_state_init(&state);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, loud);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices[1].volume == 15U);
    assert(state.voices[1].pan == 0);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_PAN_SEND) ==
           0xE000U);

    /* Poison both level registers so the assertions below can only pass if
     * the refresh path actually rewrote each of them. */
    register_words[(1U * SM64_SATURN_SCSP_SLOT_BYTES +
                    SM64_SATURN_SCSP_SLOT_ATTENUATION) / 2U] = 0xFFFFU;
    register_words[(1U * SM64_SATURN_SCSP_SLOT_BYTES +
                    SM64_SATURN_SCSP_SLOT_PAN_SEND) / 2U] = 0xFFFFU;
    register_words[(1U * SM64_SATURN_SCSP_SLOT_BYTES +
                    SM64_SATURN_SCSP_SLOT_KEYS) / 2U] = 0U;
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, quiet);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.sfx_refreshes == 1U);
    /* New level reached the hardware... */
    assert(state.voices[1].volume == 8U);
    assert(state.voices[1].pan == -15);
    assert(slot_word(register_words, 1U,
                     SM64_SATURN_SCSP_SLOT_ATTENUATION) == 0U);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_PAN_SEND) ==
           0x9800U);
    /* ...without re-keying the slot. */
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
}

/* The coalescing must be per sample, not per slot: two different sounds are
 * two voices on two slots, each keyed on once. */
static void test_distinct_sounds_still_key_on_separately(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t first[7] = {0x2400U, 0x8080U, 9U, 1U, 0xFF40U, 4096U, 1U};
    const uint16_t second[7] = {0x2400U, 0x8081U, 9U, 1U, 0xFF40U, 4096U, 2U};

    publish_v2_header(ram);
    publish_distinct_sound_bundle(ram, 2U, false);
    sm64_saturn_pcm_voice_state_init(&state);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, first);
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, second);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 2U);
    assert(state.voices_started == 2U);
    assert(state.sfx_refreshes == 0U);
    assert(state.invalid_samples == 0U);
    assert(state.voices[1].active && state.voices[1].sample_id == 0U);
    assert(state.voices[2].active && state.voices[2].sample_id == 1U);
    assert(!state.voices[SM64_SATURN_PCM_MUSIC_SLOT].active);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1810U);
    assert(slot_word(register_words, 2U, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1810U);
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
    /* Task 6 slot pinning: the first SFX voice lands on slot 1; the music
     * slot's keys word is never written by the SFX path. */
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1810U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    assert(register_word(register_words, SM64_SATURN_SCSP_MASTER_OFFSET) ==
           0x0209U);

    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_RESET, reset);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) ==
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
    /* Task 6 slot pinning: semantic SFX start at slot 1, not the music slot. */
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_SA_LOW) ==
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

/* Task 5 packager change: the looped-music row publishes
 * SM64_SATURN_PCM_SAMPLE_LOOP in its sample-row flags word.  The bundle
 * validator accepts exactly that bit -- which must reach the SCSP keys word
 * as LOOP_NORMAL -- and keeps failing closed on any unknown flag bit. */
static void test_bundle_accepts_loop_flag_and_rejects_unknown_flags(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0x2400U, 0x8080U, 9U, 1U, 0xFF40U, 4096U, 1U};
    const uint16_t flags_offset = (uint16_t)(
        SM64_SATURN_PCM_SFX_BUNDLE_OFFSET +
        SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES +
        SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES + 10U);

    publish_v2_header(ram);
    publish_sfx_bundle(ram, 0x24008080U, 1U,
                       SM64_SATURN_PCM_BANK_OFFSET, 32U, 16000U);
    sm64_saturn_pcm_put_be16(ram, flags_offset, SM64_SATURN_PCM_SAMPLE_LOOP);
    sm64_saturn_pcm_voice_state_init(&state);
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.invalid_samples == 0U);
    /* KEY_EXECUTE | KEY_ON | PCM8 | LOOP_NORMAL for a bank-offset sample,
     * on SFX slot 1 (Task 6 pins slot 0 to music). */
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_KEYS) ==
           0x1830U);

    /* Any flag bit other than the loop bit stays an invalid bundle. */
    sm64_saturn_pcm_put_be16(ram, flags_offset, 2U);
    put_sfx(ram, 1U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    assert(state.invalid_samples == 1U);
}

/* Task 6: SEQ_START starts the trailer-indexed looped music row on the
 * pinned music slot; every SEQ_START keys existing music off first. */
static void test_seq_start_starts_looped_music_on_slot0(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 34U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t restart[7] = {0U, 35U, 0U, 0U, 0U, 0U, 0U};

    publish_v2_header(ram);
    publish_music_bundle(ram, 1U, SM64_SATURN_PCM_SAMPLE_LOOP);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.unknown_opcodes == 0U);
    assert(state.music_active == 1U);
    assert(state.music_sequence_starts == 1U);
    assert(state.music_notes_started == 1U);
    assert(state.music_faults == 0U);
    assert(state.music_scsp_failures == 0U);
    assert(state.voices_started == 1U);
    assert(state.voices[SM64_SATURN_PCM_MUSIC_SLOT].active);
    /* KEY_EXECUTE | KEY_ON | PCM8 | LOOP_NORMAL on the pinned music slot. */
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0x1830U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_SA_LOW) ==
           SM64_SATURN_PCM_BANK_OFFSET + 32U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_STARTS_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_NOTES_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_SEQUENCE_OFFSET) == 34U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET) == 1U);

    /* Replace semantics: a second SEQ_START keys the old music off before
     * starting again, so exactly one voice stays active on the music slot
     * and its keys word is rewritten as a fresh looped key-on. */
    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, restart);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 1U);
    assert(state.music_sequence_starts == 2U);
    assert(state.keyoffs == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET) == 1U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0x1830U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_SEQUENCE_OFFSET) == 35U);
}

static void test_seq_stop_keys_off_music(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 34U, 0U, 0U, 0U, 0U, 0U};
    const uint16_t zero[7] = {0};

    publish_v2_header(ram);
    publish_music_bundle(ram, 1U, SM64_SATURN_PCM_SAMPLE_LOOP);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 1U);

    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_SEQ_STOP, zero);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 0U);
    assert(state.keyoffs == 1U);
    assert(!state.voices[SM64_SATURN_PCM_MUSIC_SLOT].active);
    /* scsp_pcm8_stop leaves only KEY_EXECUTE in the slot's keys word. */
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0x1000U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET) == 0U);

    /* Idempotent: a second SEQ_STOP keys nothing off again. */
    put_control(ram, 2U, SM64_SATURN_AUDIO_OPCODE_SEQ_STOP, zero);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 3U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 0U);
    assert(state.keyoffs == 1U);
}

static void test_sfx_never_uses_slot0_while_music_plays(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 34U, 0U, 0U, 0U, 0U, 0U};
    uint16_t i;

    publish_v2_header(ram);
    /* Three DISTINCT sounds: since the R1 fix a repeat of one sound refreshes
     * its slot instead of keying on again, so filling slots 1..3 needs three
     * different sounds.  Music is row 3, named by the trailer. */
    publish_distinct_sound_bundle(ram, 3U, true);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    for (i = 0U; i < 3U; ++i) {
        const uint16_t play[7] = {0x2400U, (uint16_t)(0x8080U + i), 9U, 1U,
                                  0xFF40U, 4096U, (uint16_t)(i + 1U)};
        put_sfx(ram, i, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    }
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 3U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 4U);
    assert(state.voices_started == 4U);
    assert(state.invalid_samples == 0U);
    assert(state.music_active == 1U);
    /* The music slot's registers still belong to the looped music sample. */
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0x1830U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_SA_LOW) ==
           SM64_SATURN_PCM_BANK_OFFSET + 32U);
    /* All three SFX landed on slots 1..3. */
    for (i = 1U; i < SM64_SATURN_PCM_VOICE_COUNT; ++i) {
        assert(state.voices[i].active);
        assert(slot_word(register_words, i, SM64_SATURN_SCSP_SLOT_SA_LOW) ==
               SM64_SATURN_PCM_BANK_OFFSET);
        assert(slot_word(register_words, i, SM64_SATURN_SCSP_SLOT_KEYS) ==
               0x1810U);
    }
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET) == 4U);
}

static void test_seq_start_without_music_row_is_silent_not_fault(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 34U, 0U, 0U, 0U, 0U, 0U};

    publish_v2_header(ram);
    /* Real production bundle shape: no trailer words, music_sample_index 0. */
    publish_sfx_bundle(ram, 0x24008080U, 1U,
                       SM64_SATURN_PCM_BANK_OFFSET, 32U, 16000U);
    sm64_saturn_pcm_voice_state_init(&state);
    state.music_active = 1U; /* stale music must still be silenced */
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 0U);
    assert(state.music_faults == 0U);
    assert(state.music_sequence_starts == 0U);
    assert(state.music_notes_started == 0U);
    assert(state.voices_started == 0U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET) == 0U);
}

static void test_seq_start_with_invalid_music_row_faults(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t start[7] = {0U, 34U, 0U, 0U, 0U, 0U, 0U};

    publish_v2_header(ram);
    /* Nonzero trailer index pointing at a row WITHOUT the loop bit. */
    publish_music_bundle(ram, 1U, 0U);
    sm64_saturn_pcm_voice_state_init(&state);
    put_control(ram, 0U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 0U);
    assert(state.music_faults == 1U);
    assert(state.music_sequence_starts == 0U);
    assert(state.voices_started == 0U);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               ram, SM64_SATURN_PCM_MUSIC_STARTS_OFFSET) == 0U);

    /* A trailer index outside the sample table is the same fault class. */
    sm64_saturn_pcm_put_be16(ram,
        (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_OFFSET +
                   SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SAMPLE_INDEX_FIELD), 7U);
    put_control(ram, 1U, SM64_SATURN_AUDIO_OPCODE_SEQ_START, start);
    sm64_saturn_pcm_put_be16(ram,
        SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 2U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.music_active == 0U);
    assert(state.music_faults == 2U);
    assert(state.voices_started == 0U);
}

/* The SFX rotor's entry wrap must recover from corrupt state: even a wild
 * next_slot value can never let an SFX voice land on the pinned music slot. */
static void test_sfx_slot_rotor_recovers_from_corrupt_state(void)
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
    state.next_slot = 0xFFFFU; /* corrupt rotor */
    put_sfx(ram, 0U, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, play);
    sm64_saturn_pcm_put_be16(ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 1U);
    assert(sm64_saturn_pcm68k_consume_scsp(ram, registers, &state) == 1U);
    assert(state.voices_started == 1U);
    /* The entry wrap re-bounded the rotor: the SFX started on slot 1. */
    assert(state.voices[1].active);
    assert(!state.voices[SM64_SATURN_PCM_MUSIC_SLOT].active);
    assert(slot_word(register_words, 1U, SM64_SATURN_SCSP_SLOT_SA_LOW) ==
           SM64_SATURN_PCM_BANK_OFFSET);
    assert(slot_word(register_words, SM64_SATURN_PCM_MUSIC_SLOT,
                     SM64_SATURN_SCSP_SLOT_KEYS) == 0U);
    /* And the rotor resumed in range. */
    assert(state.next_slot == 1U);
}

static void test_voice_state_fits_reserved_stack(void)
{
    /* linker.ld reserves 0x3C00..0x3FFC (1,020 bytes). The state no longer
       lives on the stack, but keep it small enough that moving it back
       could never overflow again. */
    assert(sizeof(sm64_saturn_pcm_voice_state_t) <= 768);
}

int main(void)
{
    test_voice_state_fits_reserved_stack();
    test_proof_metadata_is_deterministic_and_bounded();
    test_seq_start_is_consumed_and_keys_music_off();
    test_consumer_drains_control_before_sfx_under_budget();
    test_reset_play_invalid_and_unknown_are_safe();
    test_version_and_either_corrupt_ring_fail_before_consumption();
    test_consumer_drives_scsp_play_master_and_reset();
    test_semantic_sfx_uses_validated_sound_ram_bundle();
    test_bundle_accepts_loop_flag_and_rejects_unknown_flags();
    test_seq_start_starts_looped_music_on_slot0();
    test_seq_stop_keys_off_music();
    test_sfx_never_uses_slot0_while_music_plays();
    test_seq_start_without_music_row_is_silent_not_fault();
    test_seq_start_with_invalid_music_row_faults();
    test_sfx_slot_rotor_recovers_from_corrupt_state();
    test_repeated_play_refresh_updates_without_rekey();
    test_repeated_play_refresh_still_applies_volume_and_pan();
    test_distinct_sounds_still_key_on_separately();
    return 0;
}
