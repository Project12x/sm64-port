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

/* The MC68000 consumer is scheduled at the SCSP service cadence (~240 Hz on
 * the Saturn).  A semantic sequence tick therefore belongs to every consumer
 * poll; a large divider makes a live level appear silent for seconds and is
 * not an audio-rate implementation. */
enum { SM64_SATURN_PCM_MUSIC_POLLS_PER_TICK = 1U };

/* The target scalar sequence/voice ABI currently rejects the same note
 * request that the host model accepts (velocity/envelope fields arrive
 * corrupted on the MC68000).  Keep the semantic SEQ_START command and the
 * already-attested package, but use its bounded music sample directly until
 * that ABI is repaired.  This is deliberately a bypass, not a new wire
 * format: the sequence trailer remains present and the sequence VM remains
 * diagnostic-only when the target scalar ABI rejects a note request. */
#define SM64_SATURN_PCM_MUSIC_DIRECT_FALLBACK 1

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

static uint16_t sm64_saturn_pcm_music_period(uint16_t sample_count,
                                              uint16_t sample_rate)
{
    uint32_t numerator = (uint32_t)sample_count * 240U;
    uint16_t period = 0U;
    if (sample_rate == 0U) return 1U;
    while (numerator >= sample_rate && period != 0xffffU) {
        numerator -= sample_rate;
        ++period;
    }
    if (numerator != 0U && period != 0xffffU) ++period;
    return period == 0U ? 1U : period;
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
    state->music_vm = (sm64_saturn_sequence_vm_t){0};
    sm64_saturn_audio_engine_init(&state->music_engine, 1U);
    state->music_generation = 0U;
    state->music_sequence_offset = 0U;
    state->music_sequence_bytes = 0U;
    state->music_sample_index = 0U;
    state->music_poll_divider = 0U;
    state->music_fallback_ticks = 0U;
    state->music_fallback_period = 0U;
    state->music_active = 0U;
    state->music_direct_fallback = 0U;
    state->music_sequence_id = 0U;
    state->music_sequence_starts = 0U;
    state->music_notes_started = 0U;
    state->music_faults = 0U;
    state->music_consume_failures = 0U;
    state->music_scsp_failures = 0U;
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

static bool sm64_saturn_pcm_music_start(
    sm64_saturn_pcm_voice_state_t *state, volatile uint8_t *sound_ram,
    volatile uint8_t *scsp_registers, const uint16_t words[7])
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint32_t sequence_offset = sm64_saturn_pcm_get_be32(
        sound_ram, (uint16_t)(base +
            SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SEQUENCE_OFFSET_FIELD));
    const uint16_t sequence_bytes = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(base +
            SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SEQUENCE_BYTES_FIELD));
    const uint16_t sample_index = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(base +
            SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SAMPLE_INDEX_FIELD));
    const uint16_t generation = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(base + 8U));
    sm64_saturn_pcm_sample_t sample;
    uint16_t reject_mask = 0U;
    if (state == 0) reject_mask |= 0x0001U;
#if !defined(SM64_SATURN_PCM_MAPPED_ZERO)
    if (sound_ram == 0) reject_mask |= 0x0002U;
#endif
    if (words == 0 || (words != 0 && words[1] != 3U)) reject_mask |= 0x0004U;
    if (sequence_offset < SM64_SATURN_PCM_BANK_OFFSET) reject_mask |= 0x0008U;
    if (sequence_bytes == 0U) reject_mask |= 0x0010U;
    if (sequence_offset + sequence_bytes > SM64_SATURN_PCM_SOUND_RAM_BYTES)
        reject_mask |= 0x0020U;
    if (generation == 0U) reject_mask |= 0x0040U;
    if (!sm64_saturn_pcm_sfx_sample_descriptor(sound_ram, sample_index,
                                               &sample))
        reject_mask |= 0x0080U;
    if (state != 0) {
        sm64_saturn_pcm_put_be16(
            sound_ram, SM64_SATURN_PCM_MUSIC_SEQUENCE_OFFSET,
            (uint16_t)sequence_offset);
        sm64_saturn_pcm_put_be16(
            sound_ram, SM64_SATURN_PCM_MUSIC_SEQUENCE_BYTES_OFFSET,
            sequence_bytes);
        sm64_saturn_pcm_put_be16(
            sound_ram, SM64_SATURN_PCM_MUSIC_REJECT_MASK_OFFSET,
            reject_mask);
    }
    if (reject_mask != 0U) {
        if (state != 0) state->music_faults++;
        return false;
    }
    sm64_saturn_sequence_vm_init_ex(
        &state->music_vm, sequence_bytes, 0U,
        SM64_SATURN_SEQUENCE_VM_SEQUENCE,
        SM64_SATURN_SEQUENCE_VM_FORMAT_US);
    sm64_saturn_audio_engine_init(&state->music_engine, generation);
    state->music_generation = generation;
    state->music_sequence_offset = sequence_offset;
    state->music_sequence_bytes = sequence_bytes;
    state->music_sample_index = sample_index;
    state->music_poll_divider = 0U;
    state->music_sequence_id = (uint8_t)words[1];
    state->music_active = 1U;
    state->music_sequence_starts++;
#if SM64_SATURN_PCM_MUSIC_DIRECT_FALLBACK
    {
        const uint32_t before = state->voices_started;
        state->music_direct_fallback = 1U;
        state->music_fallback_ticks = 0U;
        state->music_fallback_period = sm64_saturn_pcm_music_period(
            sample.sample_count, sample.sample_rate);
        sm64_saturn_pcm_play_sample(state, sample_index, &sample,
                                    sample.default_volume, 0,
                                    scsp_registers);
        if (state->voices_started == before) {
            state->music_faults++;
            state->music_scsp_failures++;
            state->music_active = 0U;
            return false;
        }
        state->music_notes_started++;
        return true;
    }
#else
    (void)sample;
#endif
    return true;
}

static void sm64_saturn_pcm_music_service(
    sm64_saturn_pcm_voice_state_t *state, volatile uint8_t *sound_ram,
    volatile uint8_t *scsp_registers)
{
    sm64_saturn_sequence_vm_event_t events[8];
    sm64_saturn_pcm_sample_t sample;
    sm64_saturn_audio_note_binding_t binding;
    uint8_t event_count = 0U;
    uint8_t index;
#if !defined(SM64_SATURN_PCM_MAPPED_ZERO)
    if (state == 0 || sound_ram == 0 || !state->music_active) return;
#else
    if (state == 0 || !state->music_active) return;
#endif
    if (++state->music_poll_divider < SM64_SATURN_PCM_MUSIC_POLLS_PER_TICK)
        return;
    state->music_poll_divider = 0U;
    if (state->music_direct_fallback) {
        sm64_saturn_pcm_sample_t fallback_sample;
        const uint32_t before = state->voices_started;
        if (++state->music_fallback_ticks < state->music_fallback_period)
            return;
        state->music_fallback_ticks = 0U;
        if (!sm64_saturn_pcm_sfx_sample_descriptor(
                sound_ram, state->music_sample_index, &fallback_sample)) {
            state->music_faults++;
            state->music_active = 0U;
            return;
        }
        sm64_saturn_pcm_play_sample(
            state, state->music_sample_index, &fallback_sample,
            fallback_sample.default_volume, 0, scsp_registers);
        if (state->voices_started == before) {
            state->music_faults++;
            state->music_scsp_failures++;
            state->music_active = 0U;
        } else {
            state->music_notes_started++;
        }
        return;
    }
    if (!sm64_saturn_pcm_sfx_sample_descriptor(sound_ram,
                                               state->music_sample_index,
                                               &sample) ||
        !sm64_saturn_sequence_vm_tick(
            &state->music_vm,
            (const uint8_t *)(sound_ram + state->music_sequence_offset),
            state->music_sequence_bytes, events,
            (uint8_t)(sizeof(events) / sizeof(events[0])), &event_count)) {
        state->music_faults++;
        state->music_active = 0U;
        return;
    }
    for (index = 0U; index < event_count; ++index) {
        sm64_saturn_voice_allocation_t allocation;
        if (events[index].type == SM64_SATURN_SEQUENCE_VM_EVENT_END) {
            sm64_saturn_sequence_vm_init_ex(
                &state->music_vm, state->music_sequence_bytes, 0U,
                SM64_SATURN_SEQUENCE_VM_SEQUENCE,
                SM64_SATURN_SEQUENCE_VM_FORMAT_US);
            continue;
        }
        if (events[index].type ==
                SM64_SATURN_SEQUENCE_VM_EVENT_CHANNEL_START &&
            events[index].arg1 < state->music_sequence_bytes) {
            /* The source sequence's channel target is a layer script in the
             * same resident m64 span.  Keep the existing bounded VM and
             * switch it to the small-layer decoder; on layer END the code
             * above returns to the sequence dispatcher. */
            sm64_saturn_sequence_vm_init_ex(
                &state->music_vm, state->music_sequence_bytes,
                events[index].arg1, SM64_SATURN_SEQUENCE_VM_LAYER_SMALL,
                SM64_SATURN_SEQUENCE_VM_FORMAT_US);
            continue;
        }
        if (events[index].type != SM64_SATURN_SEQUENCE_VM_EVENT_NOTE) continue;
        binding.package_generation = state->music_generation;
        binding.sound_ram_offset = sample.sound_ram_offset;
        binding.sample_count = sample.sample_count;
        binding.loop_start = 0U;
        binding.sample_rate = sample.sample_rate;
        binding.tuning_q12 = 4096U;
        binding.sustain_q15 = 0x7FFFU;
        binding.root_note = 60;
        binding.pan = 0;
        binding.priority = 255U;
        binding.source_class = SM64_SATURN_VOICE_CLASS_MUSIC;
        binding.loop = 0U;
        binding.attack_ticks = 1U;
        binding.decay_ticks = 1U;
        binding.release_ticks = 2U;
        if (!sm64_saturn_audio_engine_consume_sequence_event(
                &state->music_engine, state->music_generation, &events[index],
                &binding, &allocation)) {
            state->music_faults++;
            state->music_consume_failures++;
            sm64_saturn_pcm_put_be16(
                sound_ram, SM64_SATURN_PCM_MUSIC_LAST_NOTE_OFFSET,
                (uint16_t)events[index].signed_value);
            sm64_saturn_pcm_put_be16(
                sound_ram, SM64_SATURN_PCM_MUSIC_LAST_ARG1_OFFSET,
                events[index].arg1);
            sm64_saturn_pcm_put_be16(
                sound_ram, SM64_SATURN_PCM_MUSIC_LAST_ARG0_OFFSET,
                events[index].arg0);
            continue;
        }
        if (scsp_registers != 0 && sm64_saturn_scsp_pcm8_start(
                scsp_registers,
                (uint16_t)(allocation.voice_index % SM64_SATURN_PCM_VOICE_COUNT),
                &sample,
                (uint16_t)(events[index].arg1 >> 8), 0)) {
            state->music_notes_started++;
            /* Keep the existing mailbox telemetry authoritative for both
             * semantic SFX and the music path.  The target proof reads this
             * counter; a music-only voice must not be invisible to it. */
            state->voices_started++;
        } else {
            state->music_faults++;
            state->music_scsp_failures++;
        }
    }
    if (!state->music_engine.has_service_generation) {
        if (!sm64_saturn_audio_engine_service_generation(
                &state->music_engine, state->music_generation)) {
            state->music_faults++;
        }
    } else {
        /* A sequence remains in one package generation for its lifetime;
         * servicing it with the same generation is valid and must not be
         * reported as a duplicate publication. */
        sm64_saturn_voice_allocator_tick(&state->music_engine.allocator);
    }
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
    state->music_active = 0U;
    state->music_direct_fallback = 0U;
    state->music_fallback_ticks = 0U;
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
    /* These fields are part of the v2 mailbox ABI and were previously left
     * unwritten by the consumer.  Publish the semantic VM cadence and live
     * allocator occupancy so a target run can distinguish a stopped VM from
     * a rejected SCSP voice without adding a second diagnostic channel. */
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET,
        (uint16_t)state->music_vm.tick_count);
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
        SM64_SATURN_PCM_MUSIC_VM_TICKS_OFFSET,
        (uint16_t)state->music_vm.tick_count);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET,
        (uint16_t)state->music_active);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_NOTES_OFFSET,
        (uint16_t)state->music_notes_started);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_MALFORMED_OFFSET,
        (uint16_t)state->music_engine.malformed_events);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_DROPPED_OFFSET,
        (uint16_t)state->music_engine.allocator.dropped_music);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_CONSUME_FAIL_OFFSET,
        (uint16_t)state->music_consume_failures);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_SCSP_FAIL_OFFSET,
        (uint16_t)state->music_scsp_failures);
    sm64_saturn_pcm_put_be16(sound_ram,
        SM64_SATURN_PCM_MUSIC_LAST_FAILURE_OFFSET,
        state->music_engine.last_failure);
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
                if (!sm64_saturn_pcm_music_start(state, sound_ram,
                                                 scsp_registers, words)) {
                    state->music_active = 0U;
                }
                break;
            case SM64_SATURN_AUDIO_OPCODE_SEQ_STOP:
                state->music_active = 0U;
                state->music_direct_fallback = 0U;
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
    sm64_saturn_pcm_music_service(state, sound_ram, scsp_registers);
    /* Music is serviced after the command-ring publication. Publish once
     * more so the existing mailbox counters describe the SCSP state reached
     * by this same consumer poll, rather than lagging by one poll. */
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
