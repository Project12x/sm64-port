#include "pcm_voice.h"

#include "saturn_pcm_protocol.h"

#if defined(__GNUC__) || defined(__clang__)
#define SM64_SATURN_PCM_CONSUME_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SM64_SATURN_PCM_CONSUME_BARRIER() ((void)0)
#endif

#ifndef SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER
#define SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(offset) ((void)(offset))
#endif

/* Metadata for the deterministic public-domain proof waveforms generated in
 * the next audio increment. No sample bytes are embedded here. */
static const sm64_saturn_pcm_sample_t s_proof_samples[] = {
    {SM64_SATURN_PCM_BANK_OFFSET + 0U, 1102U, 11025U, 12U, 0U},
    {SM64_SATURN_PCM_BANK_OFFSET + 1102U, 1102U, 11025U, 12U, 0U},
    {SM64_SATURN_PCM_BANK_OFFSET + 2204U, 2204U, 11025U, 15U, 0U},
};

static uint16_t sm64_saturn_pcm_clamp_u16(uint16_t value, uint16_t maximum)
{
    return value > maximum ? maximum : value;
}

void sm64_saturn_pcm_voice_state_init(sm64_saturn_pcm_voice_state_t *state)
{
    uint16_t i;
    if (state == 0) {
        return;
    }
    for (i = 0U; i < SM64_SATURN_PCM_VOICE_COUNT; ++i) {
        state->voices[i].active = false;
        state->voices[i].sample_id = 0U;
        state->voices[i].volume = 0U;
        state->voices[i].pan = 0;
        state->voices[i].generation = 0U;
    }
    state->commands_consumed = 0U;
    state->voices_started = 0U;
    state->keyoffs = 0U;
    state->unknown_opcodes = 0U;
    state->invalid_samples = 0U;
    state->protocol_faults = 0U;
    state->master_volume = 15U;
    state->last_opcode = SM64_SATURN_PCM_OPCODE_NOP;
    state->active_slot = 0xFFFFU;
    state->next_slot = 0U;
}

uint16_t sm64_saturn_pcm_proof_sample_count(void)
{
    return (uint16_t)(sizeof(s_proof_samples) / sizeof(s_proof_samples[0]));
}

const sm64_saturn_pcm_sample_t *sm64_saturn_pcm_proof_sample(uint16_t sample_id)
{
    const sm64_saturn_pcm_sample_t *sample;
    if (sample_id >= sm64_saturn_pcm_proof_sample_count()) {
        return 0;
    }
    sample = &s_proof_samples[sample_id];
    if ((sample->sound_ram_offset & 1U) != 0U || sample->sample_count == 0U ||
        sample->sample_rate == 0U ||
        sample->sound_ram_offset < SM64_SATURN_PCM_BANK_OFFSET ||
        sample->sound_ram_offset + sample->sample_count >
            SM64_SATURN_PCM_SOUND_RAM_BYTES) {
        return 0;
    }
    return sample;
}

static void sm64_saturn_pcm_stop_all(sm64_saturn_pcm_voice_state_t *state)
{
    uint16_t i;
    for (i = 0U; i < SM64_SATURN_PCM_VOICE_COUNT; ++i) {
        if (state->voices[i].active) {
            state->keyoffs++;
            state->voices[i].active = false;
        }
    }
    state->active_slot = 0xFFFFU;
}

static void sm64_saturn_pcm_play(sm64_saturn_pcm_voice_state_t *state,
                                 uint16_t sample_id, uint16_t volume,
                                 int16_t pan)
{
    sm64_saturn_pcm_voice_t *voice;
    if (sm64_saturn_pcm_proof_sample(sample_id) == 0) {
        state->invalid_samples++;
        return;
    }
    voice = &state->voices[state->next_slot];
    if (voice->active) {
        state->keyoffs++;
    }
    voice->active = true;
    voice->sample_id = sample_id;
    voice->volume = sm64_saturn_pcm_clamp_u16(volume, 15U);
    if (pan < -31) {
        pan = -31;
    } else if (pan > 31) {
        pan = 31;
    }
    voice->pan = pan;
    voice->generation = (uint16_t)(voice->generation + 1U);
    state->voices_started++;
    state->active_slot = state->next_slot;
    state->next_slot = (uint16_t)((state->next_slot + 1U) %
                                  SM64_SATURN_PCM_VOICE_COUNT);
}

uint16_t sm64_saturn_pcm68k_consume(volatile uint8_t *sound_ram,
                                   sm64_saturn_pcm_voice_state_t *state)
{
    uint16_t producer;
    uint16_t consumer;
    uint16_t consumed = 0U;

    if (sound_ram == 0 || state == 0) {
        return 0U;
    }
    producer = sm64_saturn_pcm_get_be16(sound_ram,
                                        SM64_SATURN_PCM_PRODUCER_OFFSET);
    consumer = sm64_saturn_pcm_get_be16(sound_ram,
                                        SM64_SATURN_PCM_CONSUMER_OFFSET);
    if (producer >= SM64_SATURN_PCM_RING_COUNT ||
        consumer >= SM64_SATURN_PCM_RING_COUNT) {
        state->protocol_faults++;
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET,
                                (uint16_t)state->protocol_faults);
        return 0U;
    }

    while (consumer != producer && consumed < SM64_SATURN_PCM_COMMANDS_PER_POLL) {
        const uint16_t base = (uint16_t)(SM64_SATURN_PCM_RING_OFFSET +
            consumer * SM64_SATURN_PCM_COMMAND_BYTES);
        const uint16_t opcode = sm64_saturn_pcm_get_be16(sound_ram, base);
        const uint16_t word0 = sm64_saturn_pcm_get_be16(sound_ram,
                                                       (uint16_t)(base + 2U));
        const uint16_t word1 = sm64_saturn_pcm_get_be16(sound_ram,
                                                       (uint16_t)(base + 4U));
        const uint16_t word2 = sm64_saturn_pcm_get_be16(sound_ram,
                                                       (uint16_t)(base + 6U));

        switch (opcode) {
            case SM64_SATURN_PCM_OPCODE_PLAY:
                sm64_saturn_pcm_play(state, word0, word1, (int16_t)word2);
                break;
            case SM64_SATURN_PCM_OPCODE_STOP_ALL:
                sm64_saturn_pcm_stop_all(state);
                break;
            case SM64_SATURN_PCM_OPCODE_SET_MASTER:
                state->master_volume = sm64_saturn_pcm_clamp_u16(word0, 15U);
                break;
            default:
                state->unknown_opcodes++;
                break;
        }
        state->last_opcode = opcode;
        state->commands_consumed++;
        consumed++;
        consumer = sm64_saturn_pcm_ring_next(consumer);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET,
                                (uint16_t)state->commands_consumed);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_VOICES_STARTED_OFFSET,
                                (uint16_t)state->voices_started);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_VOICES_STARTED_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET,
                                (uint16_t)state->unknown_opcodes);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_LAST_OPCODE_OFFSET,
                                state->last_opcode);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_LAST_OPCODE_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET,
                                state->active_slot);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET,
                                (uint16_t)state->invalid_samples);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET);
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET,
                                (uint16_t)state->protocol_faults);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET);
        SM64_SATURN_PCM_CONSUME_BARRIER();
        sm64_saturn_pcm_put_be16(sound_ram,
                                SM64_SATURN_PCM_CONSUMER_OFFSET, consumer);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
            SM64_SATURN_PCM_CONSUMER_OFFSET);
    }
    return consumed;
}
