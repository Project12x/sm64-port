#include <assert.h>
#include <stdint.h>

#include "audio_engine.h"

_Static_assert(sizeof(sm64_saturn_sequence_vm_event_t) == 12U,
               "Task 15 event must remain a compact scalar record");

static sm64_saturn_voice_request_t request(uint32_t note_id, uint8_t source_class,
                                           uint8_t priority)
{
    sm64_saturn_voice_request_t request = {0};
    request.package_generation = 7U;
    request.note_id = note_id;
    request.sound_ram_offset = 0x10000U + note_id * 64U;
    request.sample_count = 64U;
    request.sample_rate = 22050U;
    request.tuning_q12 = 4096U;
    request.root_note = 60;
    request.note = 60;
    request.velocity = 127U;
    request.pan = 0;
    request.priority = priority;
    request.source_class = source_class;
    request.attack_ticks = 1U;
    request.decay_ticks = 1U;
    request.sustain_q15 = 0x6000U;
    request.release_ticks = 2U;
    return request;
}

static void test_capacity_priority_steal_and_protection(void)
{
    sm64_saturn_voice_allocator_t allocator;
    sm64_saturn_voice_allocation_t result;
    uint32_t id;

    sm64_saturn_voice_allocator_init(&allocator);
    for (id = 0U; id < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++id) {
        sm64_saturn_voice_request_t item =
            request(id + 1U,
                    id < 2U ? SM64_SATURN_VOICE_CLASS_MUSIC
                            : SM64_SATURN_VOICE_CLASS_SFX,
                    id < 2U ? 1U : (uint8_t)(id - 1U));
        assert(sm64_saturn_voice_allocator_start(&allocator, &item, &result));
        assert(!result.stolen);
    }
    assert(sm64_saturn_voice_allocator_active_count(&allocator) ==
           SM64_SATURN_SEMANTIC_VOICE_COUNT);

    {
        sm64_saturn_voice_request_t high =
            request(100U, SM64_SATURN_VOICE_CLASS_SFX, 31U);
        assert(sm64_saturn_voice_allocator_start(&allocator, &high, &result));
        assert(result.stolen && result.displaced_note_id == 3U);
        assert(allocator.voices[0].request.note_id == 1U);
        assert(allocator.voices[1].request.note_id == 2U);
    }
    {
        sm64_saturn_voice_request_t low =
            request(101U, SM64_SATURN_VOICE_CLASS_SFX, 0U);
        assert(!sm64_saturn_voice_allocator_start(&allocator, &low, &result));
        assert(allocator.dropped_sfx == 1U);
    }
}

static void test_equal_priority_steals_oldest_eligible_sfx(void)
{
    sm64_saturn_voice_allocator_t allocator;
    sm64_saturn_voice_allocation_t result;
    uint32_t id;
    sm64_saturn_voice_allocator_init(&allocator);
    for (id = 0U; id < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++id) {
        sm64_saturn_voice_request_t item =
            request(id + 10U, SM64_SATURN_VOICE_CLASS_SFX, 4U);
        assert(sm64_saturn_voice_allocator_start(&allocator, &item, &result));
    }
    {
        sm64_saturn_voice_request_t equal =
            request(500U, SM64_SATURN_VOICE_CLASS_SFX, 4U);
        assert(sm64_saturn_voice_allocator_start(&allocator, &equal, &result));
        assert(result.stolen && result.displaced_note_id == 10U);
    }
}

static void test_release_and_envelope_are_timer_driven(void)
{
    sm64_saturn_voice_allocator_t allocator;
    sm64_saturn_voice_allocation_t result;
    sm64_saturn_voice_request_t item =
        request(77U, SM64_SATURN_VOICE_CLASS_MUSIC, 8U);
    sm64_saturn_voice_allocator_init(&allocator);
    assert(sm64_saturn_voice_allocator_start(&allocator, &item, &result));
    assert(allocator.voices[result.voice_index].desired.envelope_word == 31U);
    assert(allocator.voices[result.voice_index].desired.release_word == 31U);
    sm64_saturn_voice_allocator_tick(&allocator);
    assert(allocator.voices[result.voice_index].desired.envelope_q15 == 0x7fffU);
    assert(allocator.voices[result.voice_index].desired.attenuation == 0U);
    sm64_saturn_voice_allocator_tick(&allocator);
    assert(allocator.voices[result.voice_index].desired.envelope_q15 == 0x6000U);
    assert(sm64_saturn_voice_allocator_release(&allocator, 77U));
    assert(allocator.voices[result.voice_index].desired.releasing);
    sm64_saturn_voice_allocator_tick(&allocator);
    assert(allocator.voices[result.voice_index].desired.active);
    sm64_saturn_voice_allocator_tick(&allocator);
    assert(!allocator.voices[result.voice_index].desired.active);
}

static void test_pitch_tuning_words_are_exact(void)
{
    uint16_t pitch = 0xffffU;
    assert(sm64_saturn_desired_voice_pitch_word(44100U, 0, 4096U, &pitch));
    assert(pitch == 0x0000U);
    assert(sm64_saturn_desired_voice_pitch_word(44100U, 12, 4096U, &pitch));
    assert(pitch == 0x0800U);
    assert(sm64_saturn_desired_voice_pitch_word(44100U, -12, 4096U, &pitch));
    assert(pitch == 0x7800U);
    assert(sm64_saturn_desired_voice_pitch_word(22050U, 0, 8192U, &pitch));
    assert(pitch == 0x0000U);
    assert(!sm64_saturn_desired_voice_pitch_word(0U, 0, 4096U, &pitch));
}

static void test_sequence_scalar_event_and_generation_guards(void)
{
    sm64_saturn_audio_engine_t engine;
    sm64_saturn_audio_note_binding_t binding = {0};
    sm64_saturn_sequence_vm_event_t event = {0};
    sm64_saturn_voice_allocation_t result;

    binding.package_generation = 9U;
    binding.sound_ram_offset = 0x18000U;
    binding.sample_count = 128U;
    binding.sample_rate = 22050U;
    binding.tuning_q12 = 4096U;
    binding.root_note = 60;
    binding.priority = 5U;
    binding.source_class = SM64_SATURN_VOICE_CLASS_MUSIC;
    binding.attack_ticks = 1U;
    binding.decay_ticks = 1U;
    binding.sustain_q15 = 0x5000U;
    binding.release_ticks = 2U;
    binding.pan = -12;

    event.type = SM64_SATURN_SEQUENCE_VM_EVENT_NOTE;
    event.opcode = 0x40U;
    event.arg0 = 24U;
    event.arg1 = (uint16_t)((100U << 8) | 3U);
    event.signed_value = 72;
    event.source_offset = 0x22U;

    sm64_saturn_audio_engine_init(&engine, 9U);
    assert(sm64_saturn_audio_engine_consume_sequence_event(
        &engine, 1U, &event, &binding, &result));
    assert(engine.allocator.voices[result.voice_index].request.note == 72);
    assert(engine.allocator.voices[result.voice_index].request.velocity == 100U);
    assert(engine.allocator.voices[result.voice_index].lifetime_remaining == 24U);
    assert(engine.allocator.voices[result.voice_index].request.note_id ==
           0x00010022U);
    assert(sm64_saturn_audio_engine_service_generation(&engine, 1U));
    assert(!sm64_saturn_audio_engine_service_generation(&engine, 1U));
    assert(engine.duplicate_generations == 1U);

    event.type = 0xffU;
    assert(!sm64_saturn_audio_engine_consume_sequence_event(
        &engine, 2U, &event, &binding, &result));
    binding.sound_ram_offset = 0x7fff0U;
    binding.sample_count = 64U;
    event.type = SM64_SATURN_SEQUENCE_VM_EVENT_NOTE;
    assert(!sm64_saturn_audio_engine_consume_sequence_event(
        &engine, 2U, &event, &binding, &result));
    assert(engine.malformed_events == 2U);
}

int main(void)
{
    test_capacity_priority_steal_and_protection();
    test_equal_priority_steals_oldest_eligible_sfx();
    test_release_and_envelope_are_timer_driven();
    test_pitch_tuning_words_are_exact();
    test_sequence_scalar_event_and_generation_guards();
    return 0;
}
