/*
 * MC68000-local desired voice construction. Source semantics are a bounded
 * integer adaptation of Project12x/sm64-port@36d015fb playback.c:1132-1372,
 * effects.c:345-457, and seqplayer.c:783-921,1403-1450,1971-2014. The N64
 * note pointers, heap ownership, floating-point mixer, and RSP command stream
 * are intentionally replaced by scalar package identities and SCSP words.
 */
#include "desired_voice.h"

static uint32_t mul_u32(uint32_t left, uint32_t right)
{
    uint32_t result = 0U;
    while (right != 0U) {
        if ((right & 1U) != 0U) result += left;
        left <<= 1;
        right >>= 1;
    }
    return result;
}

static uint32_t div_u32(uint32_t numerator, uint32_t denominator)
{
    uint32_t quotient = 0U;
    uint32_t remainder = 0U;
    uint8_t bit;
    if (denominator == 0U) return 0U;
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

static uint16_t rate_word(uint8_t ticks)
{
    uint8_t distance = ticks > 31U ? 31U : ticks;
    return (uint16_t)(32U - distance);
}

static uint16_t pan_word(int16_t pan)
{
    uint16_t magnitude;
    if (pan < -64) pan = -64;
    if (pan > 63) pan = 63;
    if (pan == 0) return 0U;
    magnitude = (uint16_t)(pan < 0 ? -pan : pan);
    magnitude = (uint16_t)((magnitude + 3U) >> 2);
    if (magnitude > 15U) magnitude = 15U;
    return (uint16_t)((pan < 0 ? 0x10U : 0U) | magnitude);
}

static void refresh_attenuation(sm64_saturn_desired_voice_t *desired)
{
    uint16_t envelope_8 = (uint16_t)(desired->envelope_q15 >> 7);
    uint16_t amplitude = (uint16_t)div_u32(
        mul_u32(envelope_8, desired->velocity), 127U);
    if (amplitude > 255U) amplitude = 255U;
    desired->attenuation = (uint16_t)(255U - amplitude);
}

bool sm64_saturn_desired_voice_request_valid(
    const sm64_saturn_voice_request_t *request)
{
    uint32_t end;
    if (request == 0 || request->package_generation == 0U ||
        request->note_id == 0U || request->sample_count == 0U ||
        request->sample_rate == 0U || request->sample_rate > 44100U ||
        request->tuning_q12 < 1024U || request->tuning_q12 > 16384U ||
        request->velocity > 127U || request->sustain_q15 > 0x7fffU ||
        request->source_class < SM64_SATURN_VOICE_CLASS_MUSIC ||
        request->source_class > SM64_SATURN_VOICE_CLASS_SFX ||
        request->attack_ticks == 0U || request->decay_ticks == 0U ||
        request->release_ticks == 0U || request->root_note < -96 ||
        request->root_note > 127 || request->note < -96 || request->note > 127)
        return false;
    end = request->sound_ram_offset + request->sample_count;
    if (end < request->sound_ram_offset ||
        request->sound_ram_offset >= SM64_SATURN_SOUND_RAM_LIMIT ||
        end > SM64_SATURN_SOUND_RAM_LIMIT)
        return false;
    if (request->loop != 0U && request->loop_start >= request->sample_count)
        return false;
    return true;
}

bool sm64_saturn_desired_voice_pitch_word(uint16_t sample_rate,
                                          int16_t semitone_delta,
                                          uint16_t tuning_q12,
                                          uint16_t *pitch_word)
{
    static const uint16_t ratio_q10[12] = {
        1024U, 1085U, 1149U, 1217U, 1290U, 1367U,
        1448U, 1534U, 1625U, 1722U, 1825U, 1933U,
    };
    int16_t octave;
    int16_t remainder;
    int16_t encoded_octave;
    uint32_t rate_q10;
    uint32_t normalized;
    uint32_t fraction;
    if (pitch_word == 0 || sample_rate == 0U || sample_rate > 44100U ||
        tuning_q12 < 1024U || tuning_q12 > 16384U ||
        semitone_delta < -96 || semitone_delta > 95)
        return false;

    octave = (int16_t)(semitone_delta / 12);
    remainder = (int16_t)(semitone_delta % 12);
    if (remainder < 0) {
        remainder = (int16_t)(remainder + 12);
        octave--;
    }
    rate_q10 = mul_u32(mul_u32(sample_rate, tuning_q12) >> 12,
                       ratio_q10[remainder]);
    while (octave > 0) {
        if (rate_q10 > 0x7fffffffU) return false;
        rate_q10 <<= 1;
        octave--;
    }
    while (octave < 0) {
        rate_q10 >>= 1;
        octave++;
    }
    if (rate_q10 == 0U) return false;

    normalized = rate_q10;
    encoded_octave = 0;
    while (normalized >= (88200U << 10) && encoded_octave < 7) {
        normalized >>= 1;
        encoded_octave++;
    }
    while (normalized < (44100U << 10) && encoded_octave > -8) {
        normalized <<= 1;
        encoded_octave--;
    }
    if (normalized < (44100U << 10) || normalized >= (88200U << 10) ||
        encoded_octave < -8 || encoded_octave > 7)
        return false;
    fraction = div_u32(normalized - (44100U << 10), 44100U);
    if (fraction > 0x3ffU) fraction = 0x3ffU;
    *pitch_word = (uint16_t)((((uint16_t)encoded_octave & 0x0fU) << 11) |
                             (uint16_t)fraction);
    return true;
}

bool sm64_saturn_desired_voice_begin(sm64_saturn_desired_voice_t *desired,
                                     const sm64_saturn_voice_request_t *request,
                                     uint8_t slot, uint32_t age)
{
    uint16_t pitch;
    uint16_t send_level;
    if (desired == 0 || slot >= SM64_SATURN_SCSP_SLOT_COUNT ||
        !sm64_saturn_desired_voice_request_valid(request) ||
        !sm64_saturn_desired_voice_pitch_word(
            request->sample_rate, (int16_t)(request->note - request->root_note),
            request->tuning_q12, &pitch))
        return false;
    *desired = (sm64_saturn_desired_voice_t){0};
    desired->package_generation = request->package_generation;
    desired->note_id = request->note_id;
    desired->age = age;
    desired->start_address_low = (uint16_t)request->sound_ram_offset;
    desired->loop_start = request->loop != 0U ? request->loop_start : 0U;
    desired->loop_end = (uint16_t)(request->sample_count - 1U);
    desired->envelope_word = (uint16_t)((rate_word(request->decay_ticks) << 6) |
                                        rate_word(request->attack_ticks));
    desired->release_word = (uint16_t)(((uint16_t)(31U -
        (request->sustain_q15 >> 10)) << 5) | rate_word(request->release_ticks));
    desired->pitch_word = pitch;
    send_level = (uint16_t)(request->velocity >> 4);
    if (send_level > 7U) send_level = 7U;
    desired->pan_send_word = (uint16_t)((send_level << 13) |
                                        (pan_word(request->pan) << 8));
    desired->key_word = (uint16_t)(SM64_SATURN_SLOT_KEY_EXECUTE |
                                   SM64_SATURN_SLOT_KEY_ON |
                                   SM64_SATURN_SLOT_PCM8 |
                                   ((request->sound_ram_offset >> 16) & 0x0fU));
    if (request->loop != 0U)
        desired->key_word = (uint16_t)(desired->key_word |
                                       SM64_SATURN_SLOT_LOOP_NORMAL);
    desired->slot = slot;
    desired->source_class = request->source_class;
    desired->priority = request->priority;
    desired->phase = SM64_SATURN_ENVELOPE_ATTACK;
    desired->attack_ticks = request->attack_ticks;
    desired->decay_ticks = request->decay_ticks;
    desired->release_ticks = request->release_ticks;
    desired->velocity = request->velocity;
    desired->active = true;
    refresh_attenuation(desired);
    return true;
}

bool sm64_saturn_desired_voice_release(sm64_saturn_desired_voice_t *desired)
{
    if (desired == 0 || !desired->active || desired->releasing) return false;
    desired->releasing = true;
    desired->phase = SM64_SATURN_ENVELOPE_RELEASE;
    desired->phase_ticks = 0U;
    desired->release_start_q15 = desired->envelope_q15;
    return true;
}

void sm64_saturn_desired_voice_tick(sm64_saturn_desired_voice_t *desired)
{
    uint32_t step;
    if (desired == 0 || !desired->active) return;
    desired->phase_ticks++;
    if (desired->phase == SM64_SATURN_ENVELOPE_ATTACK) {
        step = div_u32(0x7fffU + desired->attack_ticks - 1U,
                       desired->attack_ticks);
        if (desired->phase_ticks >= desired->attack_ticks ||
            (uint32_t)desired->envelope_q15 + step >= 0x7fffU) {
            desired->envelope_q15 = 0x7fffU;
            desired->phase = SM64_SATURN_ENVELOPE_DECAY;
            desired->phase_ticks = 0U;
        } else {
            desired->envelope_q15 = (uint16_t)(desired->envelope_q15 + step);
        }
    } else if (desired->phase == SM64_SATURN_ENVELOPE_DECAY) {
        const uint16_t sustain =
            (uint16_t)((desired->release_word >> 5) >= 31U ? 0U :
                       (31U - (desired->release_word >> 5)) << 10);
        step = div_u32((uint32_t)0x7fffU - sustain +
                           desired->decay_ticks - 1U,
                       desired->decay_ticks);
        if (desired->phase_ticks >= desired->decay_ticks ||
            desired->envelope_q15 <= sustain + step) {
            desired->envelope_q15 = sustain;
            desired->phase = SM64_SATURN_ENVELOPE_SUSTAIN;
            desired->phase_ticks = 0U;
        } else {
            desired->envelope_q15 = (uint16_t)(desired->envelope_q15 - step);
        }
    } else if (desired->phase == SM64_SATURN_ENVELOPE_RELEASE) {
        step = div_u32((uint32_t)desired->release_start_q15 +
                           desired->release_ticks - 1U,
                       desired->release_ticks);
        if (desired->phase_ticks >= desired->release_ticks ||
            desired->envelope_q15 <= step) {
            desired->envelope_q15 = 0U;
            desired->active = false;
            desired->releasing = false;
            desired->phase = SM64_SATURN_ENVELOPE_OFF;
            desired->key_word = SM64_SATURN_SLOT_KEY_EXECUTE;
        } else {
            desired->envelope_q15 = (uint16_t)(desired->envelope_q15 - step);
        }
    }
    refresh_attenuation(desired);
}
