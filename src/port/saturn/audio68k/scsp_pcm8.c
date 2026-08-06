/* Minimal SCSP PCM8 slot programming.
 *
 * Close-port of the slot-word layout, pitch equation, and key-execute-last
 * sequence in ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 * (MIT, PROJ/main.c and jo_demo/pcmsys.c). Changes: byte-addressed writes,
 * four fixed slots, validated offsets/counts, bounded integer pitch, no ADX,
 * timers, mutable cross-CPU structs, or dynamic channel scan.
 */
#include "scsp_pcm8.h"

#ifndef SM64_SATURN_SCSP_WRITE_OBSERVER
#define SM64_SATURN_SCSP_WRITE_OBSERVER(offset, value) \
    ((void)(offset), (void)(value))
#endif

enum {
    SCSP_NATIVE_RATE = 44100U,
    SCSP_KEY_EXECUTE = 0x1000U,
    SCSP_KEY_ON = 0x0800U,
    SCSP_PCM8 = 0x0010U,
    SCSP_LOOP_NORMAL = 0x0020U,
};

static void put_word(volatile uint8_t *base, uint16_t offset, uint16_t value)
{
    *(volatile uint16_t *)(void *)(base + offset) = value;
    SM64_SATURN_SCSP_WRITE_OBSERVER(offset, value);
}

static uint32_t div_u32(uint32_t numerator, uint32_t denominator)
{
    uint32_t quotient = 0U;
    uint32_t remainder = 0U;
    uint16_t bit;
    for (bit = 0U; bit < 32U; ++bit) {
        remainder = (remainder << 1) | (numerator >> 31);
        numerator <<= 1;
        quotient <<= 1;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1U;
        }
    }
    return quotient;
}

bool sm64_saturn_scsp_pcm8_pitch_word(uint16_t sample_rate,
                                      uint16_t *pitch_word)
{
    uint16_t octave = 0U;
    uint32_t base_rate = SCSP_NATIVE_RATE;
    uint32_t fns;
    if (sample_rate == 0U || pitch_word == 0 || sample_rate > SCSP_NATIVE_RATE) {
        return false;
    }
    while (base_rate > sample_rate && octave < 8U) {
        base_rate >>= 1;
        octave++;
    }
    if (base_rate == 0U || octave > 7U) {
        return false;
    }
    fns = div_u32(((uint32_t)sample_rate - base_rate) << 10, base_rate);
    if (fns > 0x03FFU) {
        return false;
    }
    *pitch_word = (uint16_t)((((uint16_t)(0U - octave) & 0x0FU) << 11) |
                             (uint16_t)fns);
    return true;
}

static uint16_t pan_word(int16_t pan)
{
    uint16_t magnitude;
    if (pan < -31) {
        pan = -31;
    } else if (pan > 31) {
        pan = 31;
    }
    if (pan == 0) {
        return 0U;
    }
    magnitude = (uint16_t)(pan < 0 ? -pan : pan);
    magnitude = (uint16_t)((magnitude + 1U) >> 1);
    if (magnitude > 15U) {
        magnitude = 15U;
    }
    return (uint16_t)((pan < 0 ? 0x10U : 0U) | magnitude);
}

bool sm64_saturn_scsp_pcm8_start(volatile uint8_t *registers, uint16_t slot,
                                 const sm64_saturn_pcm_sample_t *sample,
                                 uint16_t volume, int16_t pan)
{
    uint16_t pitch;
    uint16_t base;
    uint16_t keys;
    uint16_t send_level;
    if (registers == 0 || sample == 0 || slot >= SM64_SATURN_SCSP_VOICE_COUNT ||
        ((uintptr_t)registers & 1U) != 0U || sample->sample_count == 0U ||
        sample->sound_ram_offset >= 0x100000U ||
        sample->sound_ram_offset + sample->sample_count > 0x80000U ||
        !sm64_saturn_scsp_pcm8_pitch_word(sample->sample_rate, &pitch)) {
        return false;
    }
    base = (uint16_t)(slot * SM64_SATURN_SCSP_SLOT_BYTES);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_KEYS),
             SCSP_KEY_EXECUTE);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_SA_LOW),
             (uint16_t)sample->sound_ram_offset);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_LSA), 0U);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_LEA),
             (uint16_t)(sample->sample_count - 1U));
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_EG), 31U);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_RELEASE), 31U);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_ATTENUATION), 0U);
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_PITCH), pitch);
    if (volume > 15U) {
        volume = 15U;
    }
    {
        static const uint8_t send_levels[16] = {
            0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U,
            4U, 4U, 5U, 5U, 6U, 6U, 7U, 7U,
        };
        send_level = send_levels[volume];
    }
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_PAN_SEND),
             (uint16_t)((send_level << 13) | (pan_word(pan) << 8)));
    keys = (uint16_t)(SCSP_KEY_EXECUTE | SCSP_KEY_ON | SCSP_PCM8 |
        (uint16_t)((sample->sound_ram_offset >> 16) & 0x0FU));
    if ((sample->flags & SM64_SATURN_PCM_SAMPLE_LOOP) != 0U) {
        keys = (uint16_t)(keys | SCSP_LOOP_NORMAL);
    }
    put_word(registers, (uint16_t)(base + SM64_SATURN_SCSP_SLOT_KEYS), keys);
    return true;
}

bool sm64_saturn_scsp_pcm8_stop(volatile uint8_t *registers, uint16_t slot)
{
    if (registers == 0 || ((uintptr_t)registers & 1U) != 0U ||
        slot >= SM64_SATURN_SCSP_VOICE_COUNT) {
        return false;
    }
    put_word(registers,
             (uint16_t)(slot * SM64_SATURN_SCSP_SLOT_BYTES +
                        SM64_SATURN_SCSP_SLOT_KEYS),
             SCSP_KEY_EXECUTE);
    return true;
}

bool sm64_saturn_scsp_set_master(volatile uint8_t *registers,
                                 uint16_t volume)
{
    if (registers == 0 || ((uintptr_t)registers & 1U) != 0U) {
        return false;
    }
    if (volume > 15U) {
        volume = 15U;
    }
    put_word(registers, SM64_SATURN_SCSP_MASTER_OFFSET,
             (uint16_t)(0x0200U | volume));
    return true;
}

bool sm64_saturn_scsp_apply_slot_command(
    volatile uint8_t *registers,
    const sm64_saturn_slot_command_t *command)
{
    static const uint8_t field_offsets[] = {
        SM64_SATURN_SCSP_SLOT_KEYS,
        SM64_SATURN_SCSP_SLOT_SA_LOW,
        SM64_SATURN_SCSP_SLOT_LSA,
        SM64_SATURN_SCSP_SLOT_LEA,
        SM64_SATURN_SCSP_SLOT_EG,
        SM64_SATURN_SCSP_SLOT_RELEASE,
        SM64_SATURN_SCSP_SLOT_ATTENUATION,
        SM64_SATURN_SCSP_SLOT_PITCH,
        SM64_SATURN_SCSP_SLOT_PAN_SEND,
    };
    uint16_t offset;
    if (registers == 0 || command == 0 ||
        ((uintptr_t)registers & 1U) != 0U ||
        command->slot >= SM64_SATURN_SCSP_SLOT_COUNT ||
        command->field > SM64_SATURN_SLOT_FIELD_PAN_SEND)
        return false;
    offset = (uint16_t)(command->slot * SM64_SATURN_SCSP_SLOT_BYTES +
                        field_offsets[command->field]);
    if (offset >= SM64_SATURN_SCSP_MASTER_OFFSET) return false;
    put_word(registers, offset, command->value);
    return true;
}
