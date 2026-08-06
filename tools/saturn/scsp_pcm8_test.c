#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pcm_voice.h"
#include "scsp_pcm8.h"

typedef struct observed_write {
    uint16_t offset;
    uint16_t value;
} observed_write_t;

static observed_write_t writes[32];
static uint16_t write_count;

static void observe_write(uint16_t offset, uint16_t value)
{
    assert(write_count < 32U);
    writes[write_count].offset = offset;
    writes[write_count].value = value;
    write_count++;
}

#define SM64_SATURN_SCSP_WRITE_OBSERVER(offset, value) \
    observe_write((offset), (value))
#include "../../src/port/saturn/audio68k/scsp_pcm8.c"

static uint16_t word_at(const uint16_t *words, uint16_t byte_offset)
{
    return words[byte_offset / 2U];
}

static void test_pitch_words_are_bounded_and_exact(void)
{
    uint16_t pitch = 0xFFFFU;
    assert(!sm64_saturn_scsp_pcm8_pitch_word(0U, &pitch));
    assert(!sm64_saturn_scsp_pcm8_pitch_word(11025U, NULL));
    assert(sm64_saturn_scsp_pcm8_pitch_word(44100U, &pitch));
    assert(pitch == 0x0000U);
    assert(sm64_saturn_scsp_pcm8_pitch_word(22050U, &pitch));
    assert(pitch == 0x7800U);
    assert(sm64_saturn_scsp_pcm8_pitch_word(11025U, &pitch));
    assert(pitch == 0x7000U);
}

static void test_pcm8_start_writes_native_68k_words_and_keys_last(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U];
    uint8_t *registers = (uint8_t *)register_words;
    const sm64_saturn_pcm_sample_t sample = {
        0x08000U, 1102U, 11025U, 12U, 0U
    };
    memset(register_words, 0xA5, sizeof(register_words));
    write_count = 0U;

    assert(sm64_saturn_scsp_pcm8_start(registers, 2U, &sample, 12U, 0));
    assert(write_count == 10U);
    assert(writes[0].offset == 0x40U + SM64_SATURN_SCSP_SLOT_KEYS);
    assert(writes[0].value == 0x1000U);
    assert(writes[write_count - 1U].offset ==
           0x40U + SM64_SATURN_SCSP_SLOT_KEYS);
    assert(writes[write_count - 1U].value == 0x1810U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_SA_LOW) == 0x8000U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_LSA) == 0U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_LEA) == 1101U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_EG) == 31U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_RELEASE) == 31U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_PITCH) == 0x7000U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_PAN_SEND) == 0xC000U);
    assert(word_at(register_words, 0x40U + SM64_SATURN_SCSP_SLOT_KEYS) == 0x1810U);
}

static void test_loop_stop_and_invalid_requests_fail_closed(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_pcm_sample_t sample = {
        0x08000U, 8U, 11025U, 15U, SM64_SATURN_PCM_SAMPLE_LOOP
    };

    assert(sm64_saturn_scsp_pcm8_start(registers, 3U, &sample, 15U, -31));
    assert(word_at(register_words, 0x60U + SM64_SATURN_SCSP_SLOT_KEYS) == 0x1830U);
    assert(word_at(register_words, 0x60U + SM64_SATURN_SCSP_SLOT_PAN_SEND) == 0xFF00U);
    assert(sm64_saturn_scsp_pcm8_stop(registers, 3U));
    assert(word_at(register_words, 0x60U + SM64_SATURN_SCSP_SLOT_KEYS) == 0x1000U);

    assert(!sm64_saturn_scsp_pcm8_start(NULL, 0U, &sample, 1U, 0));
    assert(!sm64_saturn_scsp_pcm8_start(registers, 4U, &sample, 1U, 0));
    sample.sample_count = 0U;
    assert(!sm64_saturn_scsp_pcm8_start(registers, 0U, &sample, 1U, 0));
    assert(!sm64_saturn_scsp_pcm8_stop(registers, 4U));
}

static void test_master_volume_is_clamped(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    assert(sm64_saturn_scsp_set_master(registers, 99U));
    assert(word_at(register_words, SM64_SATURN_SCSP_MASTER_OFFSET) == 0x020FU);
    assert(!sm64_saturn_scsp_set_master(NULL, 1U));
}

static void test_pointer_free_shadow_command_has_one_explicit_mmio_boundary(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    uint8_t *registers = (uint8_t *)register_words;
    sm64_saturn_slot_command_t command = {
        0x4321U, 31U, SM64_SATURN_SLOT_FIELD_PITCH
    };
    write_count = 0U;
    assert(sm64_saturn_scsp_apply_slot_command(registers, &command));
    assert(write_count == 1U);
    assert(writes[0].offset == 31U * SM64_SATURN_SCSP_SLOT_BYTES +
                               SM64_SATURN_SCSP_SLOT_PITCH);
    assert(writes[0].value == 0x4321U);
    command.slot = SM64_SATURN_SCSP_SLOT_COUNT;
    assert(!sm64_saturn_scsp_apply_slot_command(registers, &command));
    command.slot = 0U;
    command.field = 0xffU;
    assert(!sm64_saturn_scsp_apply_slot_command(registers, &command));
}

int main(void)
{
    test_pitch_words_are_bounded_and_exact();
    test_pcm8_start_writes_native_68k_words_and_keys_last();
    test_loop_stop_and_invalid_requests_fail_closed();
    test_master_volume_is_clamped();
    test_pointer_free_shadow_command_has_one_explicit_mmio_boundary();
    return 0;
}
