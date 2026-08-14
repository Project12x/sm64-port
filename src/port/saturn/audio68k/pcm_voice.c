#include "pcm_voice.h"

#include "scsp_pcm8.h"
#include "saturn_pcm_protocol.h"

#if defined(__GNUC__) || defined(__clang__)
#define SM64_SATURN_PCM_CONSUME_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SM64_SATURN_PCM_CONSUME_BARRIER() ((void)0)
#endif

#ifndef SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER
#define SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(offset) ((void)(offset))
#endif

typedef struct sm64_saturn_pcm_consumer_ring {
    uint16_t producer_offset;
    uint16_t consumer_offset;
    uint16_t ring_offset;
    uint16_t ring_count;
    bool control;
} sm64_saturn_pcm_consumer_ring_t;

typedef enum sm64_saturn_pcm_sfx_bundle_state {
    SM64_SATURN_PCM_SFX_BUNDLE_ABSENT = 0,
    SM64_SATURN_PCM_SFX_BUNDLE_READY,
    SM64_SATURN_PCM_SFX_BUNDLE_INVALID,
} sm64_saturn_pcm_sfx_bundle_state_t;

typedef struct sm64_saturn_pcm_sfx_bundle_view {
    uint16_t generation;
    uint16_t mapping_count;
    uint16_t sample_count;
    uint16_t mapping_offset;
    uint16_t sample_offset;
    uint32_t pcm_bytes;
} sm64_saturn_pcm_sfx_bundle_view_t;

static void sm64_saturn_pcm_play_sample(
    sm64_saturn_pcm_voice_state_t *state, uint16_t sample_id,
    const sm64_saturn_pcm_sample_t *sample, uint16_t volume, int16_t pan,
    volatile uint8_t *scsp_registers);

/* Metadata for the deterministic public-domain proof waveforms. No sample
 * bytes are embedded here. The proof stays live while its v1 wire layout is
 * retained separately as historical contract data. */
static const sm64_saturn_pcm_sample_t s_proof_samples[] = {
    {SM64_SATURN_PCM_BANK_OFFSET + 0U, 1102U, 11025U, 12U, 0U},
    {SM64_SATURN_PCM_BANK_OFFSET + 1102U, 1102U, 11025U, 12U, 0U},
    {SM64_SATURN_PCM_BANK_OFFSET + 2204U, 2204U, 11025U, 15U, 0U},
};

static const sm64_saturn_pcm_consumer_ring_t s_consumer_control_ring = {
    SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET,
    SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_OFFSET,
    SM64_SATURN_PCM_CONTROL_RING_COUNT,
    true,
};

static const sm64_saturn_pcm_consumer_ring_t s_consumer_sfx_ring = {
    SM64_SATURN_PCM_SFX_PRODUCER_OFFSET,
    SM64_SATURN_PCM_SFX_CONSUMER_OFFSET,
    SM64_SATURN_PCM_SFX_RING_OFFSET,
    SM64_SATURN_PCM_SFX_RING_COUNT,
    false,
};

static uint16_t sm64_saturn_pcm_clamp_u16(uint16_t value, uint16_t maximum)
{
    return value > maximum ? maximum : value;
}

/* Keys the music path off.  The sequence VM and its software voice engine
 * were removed from the linked image (Task 4 driver diet); until the
 * looped-sample music path lands, keying music off only means clearing the
 * activity flag the mailbox publisher reports. */
static void sm64_saturn_pcm_music_key_off(sm64_saturn_pcm_voice_state_t *state)
{
    state->music_active = 0U;
}

static sm64_saturn_pcm_sfx_bundle_state_t
sm64_saturn_pcm_sfx_bundle_view(const volatile uint8_t *sound_ram,
                                sm64_saturn_pcm_sfx_bundle_view_t *view)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    uint16_t mapping_count;
    uint16_t sample_count;
    uint16_t mapping_offset;
    uint16_t sample_offset;
    uint16_t metadata_bytes;
    uint32_t pcm_bytes;
    uint16_t i;

    /* The target maps sound RAM at MC68000 address zero.  Keep the host API's
     * null rejection, but do not reject the deliberately mapped-zero target
     * pointer used by consume_mapped_zero(). */
#if !defined(SM64_SATURN_PCM_MAPPED_ZERO)
    if (sound_ram == 0 || view == 0) {
#else
    if (view == 0) {
#endif
        return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
    }
    if (sm64_saturn_pcm_get_be32(sound_ram, base) !=
        SM64_SATURN_PCM_SFX_BUNDLE_MAGIC) {
        return sm64_saturn_pcm_get_be32(sound_ram, base) == 0U
                   ? SM64_SATURN_PCM_SFX_BUNDLE_ABSENT
                   : SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
    }
    if (sm64_saturn_pcm_get_be16(sound_ram, (uint16_t)(base + 4U)) !=
            SM64_SATURN_PCM_SFX_BUNDLE_VERSION ||
        sm64_saturn_pcm_get_be16(sound_ram, (uint16_t)(base + 6U)) !=
            SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES) {
        return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
    }
    view->generation = sm64_saturn_pcm_get_be16(sound_ram,
                                                 (uint16_t)(base + 8U));
    mapping_count = sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(base + 10U));
    sample_count = sm64_saturn_pcm_get_be16(sound_ram,
                                             (uint16_t)(base + 12U));
    mapping_offset = sm64_saturn_pcm_get_be16(sound_ram,
                                               (uint16_t)(base + 14U));
    sample_offset = sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(base + 16U));
    metadata_bytes = sm64_saturn_pcm_get_be16(sound_ram,
                                               (uint16_t)(base + 18U));
    pcm_bytes = sm64_saturn_pcm_get_be32(sound_ram, (uint16_t)(base + 20U));
    if (view->generation == 0U || mapping_count == 0U || sample_count == 0U ||
        mapping_offset != SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES ||
        (uint32_t)sample_offset != (uint32_t)mapping_offset +
                                      (uint32_t)mapping_count *
                                          SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES ||
        (uint32_t)metadata_bytes != (uint32_t)sample_offset +
                                       (uint32_t)sample_count *
                                           SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES ||
        metadata_bytes > SM64_SATURN_PCM_SFX_BUNDLE_BYTES || pcm_bytes == 0U ||
        pcm_bytes > SM64_SATURN_PCM_SOUND_RAM_BYTES -
                        SM64_SATURN_PCM_BANK_OFFSET) {
        return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
    }
    for (i = 0U; i < sample_count; ++i) {
        const uint16_t row = (uint16_t)(base + sample_offset +
            i * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
        const uint32_t offset = sm64_saturn_pcm_get_be32(sound_ram, row);
        const uint16_t count = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 4U));
        const uint16_t rate = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 6U));
        const uint16_t volume = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 8U));
        const uint16_t flags = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 10U));
        if ((offset & 1U) != 0U || offset < SM64_SATURN_PCM_BANK_OFFSET ||
            count == 0U || rate == 0U || rate > 44100U || volume > 15U ||
            flags != 0U || offset + count < offset ||
            offset + count > SM64_SATURN_PCM_BANK_OFFSET + pcm_bytes ||
            offset + count > SM64_SATURN_PCM_SOUND_RAM_BYTES) {
            return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
        }
    }
    for (i = 0U; i < mapping_count; ++i) {
        const uint16_t row = (uint16_t)(base + mapping_offset +
            i * SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
        const uint32_t bits = sm64_saturn_pcm_get_be32(sound_ram, row);
        const uint16_t first = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 4U));
        const uint16_t count = sm64_saturn_pcm_get_be16(
            sound_ram, (uint16_t)(row + 6U));
        uint16_t previous;
        if (bits == 0U || count > SM64_SATURN_PCM_SFX_BUNDLE_CHAIN_MAX ||
            (count != 0U && (first >= sample_count ||
                              count > sample_count - first))) {
            return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
        }
        for (previous = 0U; previous < i; ++previous) {
            const uint16_t other = (uint16_t)(base + mapping_offset +
                previous * SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
            if (sm64_saturn_pcm_get_be32(sound_ram, other) == bits) {
                return SM64_SATURN_PCM_SFX_BUNDLE_INVALID;
            }
        }
    }
    view->mapping_count = mapping_count;
    view->sample_count = sample_count;
    view->mapping_offset = mapping_offset;
    view->sample_offset = sample_offset;
    view->pcm_bytes = pcm_bytes;
    return SM64_SATURN_PCM_SFX_BUNDLE_READY;
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
    state->control_commands_consumed = 0U;
    state->sfx_commands_consumed = 0U;
    state->voices_started = 0U;
    state->keyoffs = 0U;
    state->unknown_opcodes = 0U;
    state->invalid_samples = 0U;
    state->protocol_faults = 0U;
    state->master_volume = 15U;
    state->last_opcode = SM64_SATURN_AUDIO_OPCODE_NOP;
    state->active_slot = 0xFFFFU;
    state->next_slot = 0U;
    state->music_generation = 0U;
    state->music_sequence_starts = 0U;
    state->music_notes_started = 0U;
    state->music_faults = 0U;
    state->music_scsp_failures = 0U;
    state->music_active = 0U;
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

bool sm64_saturn_pcm_sfx_sample_descriptor(
    const volatile uint8_t *sound_ram, uint16_t sample_index,
    sm64_saturn_pcm_sample_t *sample)
{
    sm64_saturn_pcm_sfx_bundle_view_t view;
    uint16_t offset;
    if (sample != 0) *sample = (sm64_saturn_pcm_sample_t){0};
#if !defined(SM64_SATURN_PCM_MAPPED_ZERO)
    if (sound_ram == 0 || sample == 0 ||
#else
    if (sample == 0 ||
#endif
        sm64_saturn_pcm_sfx_bundle_view(sound_ram, &view) !=
            SM64_SATURN_PCM_SFX_BUNDLE_READY ||
        sample_index >= view.sample_count) {
        return false;
    }
    offset = (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_OFFSET +
                        view.sample_offset +
                        sample_index * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
    sample->sound_ram_offset = sm64_saturn_pcm_get_be32(sound_ram, offset);
    sample->sample_count = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(offset + 4U));
    sample->sample_rate = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(offset + 6U));
    sample->default_volume = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(offset + 8U));
    sample->flags = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(offset + 10U));
    return sample->sample_count != 0U && sample->sample_rate != 0U;
}

static void sm64_saturn_pcm_stop_all(sm64_saturn_pcm_voice_state_t *state,
                                     volatile uint8_t *scsp_registers)
{
    uint16_t i;
    for (i = 0U; i < SM64_SATURN_PCM_VOICE_COUNT; ++i) {
        if (state->voices[i].active) {
            if (scsp_registers != 0) {
                (void)sm64_saturn_scsp_pcm8_stop(scsp_registers, i);
            }
            state->keyoffs++;
            state->voices[i].active = false;
        }
    }
    state->active_slot = 0xFFFFU;
    sm64_saturn_pcm_music_key_off(state);
}

static void sm64_saturn_pcm_play_sample(
    sm64_saturn_pcm_voice_state_t *state, uint16_t sample_id,
    const sm64_saturn_pcm_sample_t *sample, uint16_t volume, int16_t pan,
    volatile uint8_t *scsp_registers)
{
    sm64_saturn_pcm_voice_t *voice;
    if (sample == 0) {
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
    if (scsp_registers != 0 &&
        !sm64_saturn_scsp_pcm8_start(scsp_registers, state->next_slot,
                                     sample, voice->volume, voice->pan)) {
        voice->active = false;
        state->invalid_samples++;
        return;
    }
    state->voices_started++;
    state->active_slot = state->next_slot;
    state->next_slot = (uint16_t)((state->next_slot + 1U) %
                                  SM64_SATURN_PCM_VOICE_COUNT);
}

static void sm64_saturn_pcm_play(sm64_saturn_pcm_voice_state_t *state,
                                 uint16_t sample_id, uint16_t volume,
                                 int16_t pan,
                                 volatile uint8_t *scsp_registers)
{
    sm64_saturn_pcm_play_sample(state, sample_id,
                                sm64_saturn_pcm_proof_sample(sample_id),
                                volume, pan, scsp_registers);
}

static uint16_t sm64_saturn_pcm_semantic_volume(uint16_t encoded,
                                                 uint16_t maximum)
{
    static const uint8_t thresholds[15] = {
        9U, 26U, 43U, 60U, 77U, 94U, 111U, 128U,
        145U, 162U, 179U, 196U, 213U, 230U, 247U,
    };
    const uint16_t normalized = (uint16_t)(encoded >> 8);
    uint16_t volume = 0U;
    while (volume < 15U && normalized >= thresholds[volume]) {
        ++volume;
    }
    return volume > maximum ? maximum : volume;
}

static int16_t sm64_saturn_pcm_semantic_pan(uint16_t encoded)
{
    static const uint8_t thresholds[62] = {
        2U, 4U, 6U, 8U, 10U, 12U, 14U, 16U, 18U, 20U, 22U,
        24U, 26U, 28U, 30U, 32U, 34U, 36U, 38U, 40U, 42U, 45U,
        47U, 49U, 51U, 53U, 55U, 57U, 59U, 61U, 63U, 65U, 67U,
        69U, 71U, 73U, 75U, 77U, 79U, 81U, 83U, 86U, 88U, 90U,
        92U, 94U, 96U, 98U, 100U, 102U, 104U, 106U, 108U, 110U,
        112U, 114U, 116U, 118U, 120U, 122U, 124U, 126U,
    };
    const uint16_t normalized = (uint16_t)(encoded & 0x00FFU);
    const uint16_t bounded = normalized > 127U ? 127U : normalized;
    uint16_t magnitude = 0U;
    while (magnitude < 62U && bounded >= thresholds[magnitude]) {
        ++magnitude;
    }
    return (int16_t)magnitude - 31;
}

static bool sm64_saturn_pcm_play_semantic(
    sm64_saturn_pcm_voice_state_t *state, volatile uint8_t *sound_ram,
    volatile uint8_t *scsp_registers, const uint16_t words[7])
{
    sm64_saturn_pcm_sfx_bundle_view_t view;
    const uint32_t sound_bits = ((uint32_t)words[0] << 16) | words[1];
    const sm64_saturn_pcm_sfx_bundle_state_t bundle =
        sm64_saturn_pcm_sfx_bundle_view(sound_ram, &view);
    uint16_t i;

    if (bundle == SM64_SATURN_PCM_SFX_BUNDLE_ABSENT) {
        /* Soundtest/proof compatibility retains its historic three-word
         * command payload only while no production bundle is present. */
        sm64_saturn_pcm_play(state, words[0], words[1], (int16_t)words[2],
                             scsp_registers);
        return true;
    }
    if (bundle != SM64_SATURN_PCM_SFX_BUNDLE_READY ||
        words[3] != view.generation) {
        state->invalid_samples++;
        return false;
    }
    for (i = 0U; i < view.mapping_count; ++i) {
        const uint16_t mapping = (uint16_t)(SM64_SATURN_PCM_SFX_BUNDLE_OFFSET +
            view.mapping_offset + i * SM64_SATURN_PCM_SFX_BUNDLE_MAPPING_BYTES);
        if (sm64_saturn_pcm_get_be32(sound_ram, mapping) == sound_bits) {
            const uint16_t first = sm64_saturn_pcm_get_be16(
                sound_ram, (uint16_t)(mapping + 4U));
            const uint16_t count = sm64_saturn_pcm_get_be16(
                sound_ram, (uint16_t)(mapping + 6U));
            uint16_t sample_index;
            if (count == 0U) {
                state->invalid_samples++;
                return false;
            }
            for (sample_index = first;
                 sample_index < (uint16_t)(first + count); ++sample_index) {
                const uint16_t sample = (uint16_t)(
                    SM64_SATURN_PCM_SFX_BUNDLE_OFFSET + view.sample_offset +
                    sample_index * SM64_SATURN_PCM_SFX_BUNDLE_SAMPLE_BYTES);
                const sm64_saturn_pcm_sample_t descriptor = {
                    sm64_saturn_pcm_get_be32(sound_ram, sample),
                    sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(sample + 4U)),
                    sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(sample + 6U)),
                    sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(sample + 8U)),
                    sm64_saturn_pcm_get_be16(sound_ram,
                                              (uint16_t)(sample + 10U)),
                };
                sm64_saturn_pcm_play_sample(
                    state, sample_index, &descriptor,
                    sm64_saturn_pcm_semantic_volume(words[4],
                                                    descriptor.default_volume),
                    sm64_saturn_pcm_semantic_pan(words[4]), scsp_registers);
            }
            return true;
        }
    }
    state->invalid_samples++;
    return false;
}

static uint16_t sm64_saturn_pcm_active_voice_count(
    const sm64_saturn_pcm_voice_state_t *state)
{
    uint16_t count = 0U;
    uint16_t i;
    if (state == 0) return 0U;
    for (i = 0U; i < SM64_SATURN_PCM_VOICE_COUNT; ++i) {
        if (state->voices[i].active) ++count;
    }
    return count;
}

static void sm64_saturn_pcm_publish_stats(
    volatile uint8_t *sound_ram, const sm64_saturn_pcm_voice_state_t *state)
{
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET,
        (uint16_t)state->commands_consumed);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_VOICES_STARTED_OFFSET,
                            (uint16_t)state->voices_started);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_VOICES_STARTED_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET,
                            (uint16_t)state->unknown_opcodes);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_LAST_OPCODE_OFFSET,
                            state->last_opcode);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_LAST_OPCODE_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET,
                            state->active_slot);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET,
                            (uint16_t)state->invalid_samples);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET,
        (uint16_t)state->control_commands_consumed);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_SFX_CONSUMED_OFFSET,
                            (uint16_t)state->sfx_commands_consumed);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_SFX_CONSUMED_OFFSET);
    /* These fields are part of the v2 mailbox ABI; every offset keeps being
     * written so the protocol layout is unchanged.  The sequence VM and its
     * software voice engine are no longer linked into the image (Task 4
     * driver diet), so the VM/engine-sourced words publish a constant 0
     * while the surviving music counters stay live. */
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET, 0U);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET,
        sm64_saturn_pcm_active_voice_count(state));
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_STARTS_OFFSET,
        (uint16_t)state->music_sequence_starts);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET,
        (uint16_t)state->music_faults);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_VM_TICKS_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET,
        (uint16_t)state->music_active);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_NOTES_OFFSET,
        (uint16_t)state->music_notes_started);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_MALFORMED_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_DROPPED_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_CONSUME_FAIL_OFFSET, 0U);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_SCSP_FAIL_OFFSET,
        (uint16_t)state->music_scsp_failures);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_LAST_FAILURE_OFFSET, 0U);
}

static void sm64_saturn_pcm_protocol_fault(
    volatile uint8_t *sound_ram, sm64_saturn_pcm_voice_state_t *state)
{
    state->protocol_faults++;
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET,
                            (uint16_t)state->protocol_faults);
    SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(
        SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET);
}

static bool sm64_saturn_pcm_ring_read(
    const volatile uint8_t *sound_ram,
    const sm64_saturn_pcm_consumer_ring_t *ring,
    uint16_t *producer, uint16_t *consumer)
{
    uint16_t occupancy;
    *producer = sm64_saturn_pcm_get_be16(sound_ram, ring->producer_offset);
    *consumer = sm64_saturn_pcm_get_be16(sound_ram, ring->consumer_offset);
    if (!sm64_saturn_pcm_ring_cursor_is_valid(*producer, ring->ring_count) ||
        !sm64_saturn_pcm_ring_cursor_is_valid(*consumer, ring->ring_count)) {
        return false;
    }
    occupancy = sm64_saturn_pcm_ring_occupancy(*producer, *consumer,
                                               ring->ring_count);
    return occupancy <= ring->ring_count;
}

static void sm64_saturn_pcm_apply_command(
    sm64_saturn_pcm_voice_state_t *state,
    volatile uint8_t *scsp_registers, bool control, uint16_t opcode,
    volatile uint8_t *sound_ram, const uint16_t words[7])
{
    if (control) {
        switch ((sm64_saturn_audio_opcode_t)opcode) {
            case SM64_SATURN_AUDIO_OPCODE_RESET:
            case SM64_SATURN_AUDIO_OPCODE_MUTE:
                sm64_saturn_pcm_stop_all(state, scsp_registers);
                break;
            case SM64_SATURN_AUDIO_OPCODE_SET_MASTER:
                state->master_volume = sm64_saturn_pcm_clamp_u16(words[0], 15U);
                if (scsp_registers != 0) {
                    (void)sm64_saturn_scsp_set_master(scsp_registers,
                                                      state->master_volume);
                }
                break;
            case SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE:
            case SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT:
            case SM64_SATURN_AUDIO_OPCODE_SEQ_FADE:
            case SM64_SATURN_AUDIO_OPCODE_SEQ_CHANNEL_FADE:
            case SM64_SATURN_AUDIO_OPCODE_BANK_MASK:
            case SM64_SATURN_AUDIO_OPCODE_SOUND_MODE:
                break;
            case SM64_SATURN_AUDIO_OPCODE_SEQ_START:
                /* Task 6 rewrites this as the looped-sample music start. */
                sm64_saturn_pcm_music_key_off(state);
                break;
            case SM64_SATURN_AUDIO_OPCODE_SEQ_STOP:
                sm64_saturn_pcm_music_key_off(state);
                break;
            default:
                state->unknown_opcodes++;
                break;
        }
    } else {
        switch ((sm64_saturn_audio_opcode_t)opcode) {
            case SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH:
                (void)sm64_saturn_pcm_play_semantic(state, sound_ram,
                                                     scsp_registers, words);
                break;
            case SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE:
            case SM64_SATURN_AUDIO_OPCODE_STOP_SOURCE:
            case SM64_SATURN_AUDIO_OPCODE_STOP_BANK:
                break;
            default:
                state->unknown_opcodes++;
                break;
        }
    }
}

static uint16_t sm64_saturn_pcm_consume_ring(
    volatile uint8_t *sound_ram, volatile uint8_t *scsp_registers,
    sm64_saturn_pcm_voice_state_t *state,
    const sm64_saturn_pcm_consumer_ring_t *ring,
    uint16_t producer, uint16_t consumer, uint16_t budget)
{
    uint16_t consumed = 0U;
    while (consumer != producer && consumed < budget) {
        const uint16_t base = (uint16_t)(ring->ring_offset +
            sm64_saturn_pcm_ring_cursor_slot(consumer, ring->ring_count) *
                SM64_SATURN_PCM_COMMAND_BYTES);
        const uint16_t opcode = sm64_saturn_pcm_get_be16(sound_ram, base);
        uint16_t words[7];
        uint16_t word;
        for (word = 0U; word < 7U; ++word) {
            words[word] = sm64_saturn_pcm_get_be16(
                sound_ram, (uint16_t)(base + 2U + word * 2U));
        }

        sm64_saturn_pcm_apply_command(state, scsp_registers, ring->control,
                                      opcode, sound_ram, words);
        state->last_opcode = opcode;
        state->commands_consumed++;
        if (ring->control) {
            state->control_commands_consumed++;
        } else {
            state->sfx_commands_consumed++;
        }
        consumed++;
        consumer = sm64_saturn_pcm_ring_cursor_next(consumer,
                                                    ring->ring_count);
        sm64_saturn_pcm_publish_stats(sound_ram, state);
        SM64_SATURN_PCM_CONSUME_BARRIER();
        sm64_saturn_pcm_put_be16(sound_ram, ring->consumer_offset, consumer);
        SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(ring->consumer_offset);
    }
    return consumed;
}

static uint16_t sm64_saturn_pcm68k_consume_internal(
    volatile uint8_t *sound_ram, volatile uint8_t *scsp_registers,
    sm64_saturn_pcm_voice_state_t *state)
{
    uint16_t control_producer;
    uint16_t control_consumer;
    uint16_t sfx_producer;
    uint16_t sfx_consumer;
    uint16_t consumed;

    if (state == 0) {
        return 0U;
    }
    if (sm64_saturn_pcm_get_be16(sound_ram, SM64_SATURN_PCM_MAGIC_OFFSET) !=
            SM64_SATURN_PCM_PROTOCOL_MAGIC ||
        sm64_saturn_pcm_get_be16(sound_ram, SM64_SATURN_PCM_VERSION_OFFSET) !=
            SM64_SATURN_PCM_PROTOCOL_VERSION ||
        !sm64_saturn_pcm_ring_read(sound_ram, &s_consumer_control_ring,
                                   &control_producer, &control_consumer) ||
        !sm64_saturn_pcm_ring_read(sound_ram, &s_consumer_sfx_ring,
                                   &sfx_producer, &sfx_consumer)) {
        sm64_saturn_pcm_protocol_fault(sound_ram, state);
        return 0U;
    }

    consumed = sm64_saturn_pcm_consume_ring(
        sound_ram, scsp_registers, state, &s_consumer_control_ring,
        control_producer,
        control_consumer, SM64_SATURN_PCM_COMMANDS_PER_POLL);
    consumed = (uint16_t)(consumed + sm64_saturn_pcm_consume_ring(
        sound_ram, scsp_registers, state, &s_consumer_sfx_ring, sfx_producer,
        sfx_consumer,
        (uint16_t)(SM64_SATURN_PCM_COMMANDS_PER_POLL - consumed)));
    /* Publish once per poll even when no command was consumed so the
     * mailbox counters always describe the SCSP state reached by this
     * consumer poll. */
    sm64_saturn_pcm_publish_stats(sound_ram, state);
    return consumed;
}

uint16_t sm64_saturn_pcm68k_consume(volatile uint8_t *sound_ram,
                                   sm64_saturn_pcm_voice_state_t *state)
{
    if (sound_ram == 0) {
        return 0U;
    }
    return sm64_saturn_pcm68k_consume_internal(sound_ram, 0, state);
}

uint16_t sm64_saturn_pcm68k_consume_scsp(volatile uint8_t *sound_ram,
                                        volatile uint8_t *scsp_registers,
                                        sm64_saturn_pcm_voice_state_t *state)
{
    if (sound_ram == 0) {
        return 0U;
    }
    return sm64_saturn_pcm68k_consume_internal(sound_ram, scsp_registers,
                                               state);
}

uint16_t sm64_saturn_pcm68k_consume_mapped_zero(
    volatile uint8_t *scsp_registers,
    sm64_saturn_pcm_voice_state_t *state)
{
    volatile uint8_t *const sound_ram = (volatile uint8_t *)(uintptr_t)0;
    return sm64_saturn_pcm68k_consume_internal(sound_ram, scsp_registers,
                                               state);
}
