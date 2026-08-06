#include "audio_engine.h"

static bool generation_newer(uint32_t candidate, uint32_t previous)
{
    uint32_t delta = candidate - previous;
    return delta != 0U && delta < 0x80000000U;
}

static uint32_t mul_u16(uint16_t left, uint16_t right)
{
    uint32_t result = 0U;
    uint32_t value = left;
    while (right != 0U) {
        if ((right & 1U) != 0U) result += value;
        value <<= 1;
        right >>= 1;
    }
    return result;
}

void sm64_saturn_audio_engine_init(sm64_saturn_audio_engine_t *engine,
                                   uint32_t active_package_generation)
{
    if (engine == 0) return;
    *engine = (sm64_saturn_audio_engine_t){0};
    engine->active_package_generation = active_package_generation;
    sm64_saturn_voice_allocator_init(&engine->allocator);
}

bool sm64_saturn_audio_engine_consume_sequence_event(
    sm64_saturn_audio_engine_t *engine, uint32_t event_generation,
    const sm64_saturn_sequence_vm_event_t *event,
    const sm64_saturn_audio_note_binding_t *binding,
    sm64_saturn_voice_allocation_t *allocation)
{
    sm64_saturn_voice_request_t request = {0};
    uint8_t duration;
    uint32_t lifetime;
    if (engine == 0 || event == 0 || binding == 0 || allocation == 0 ||
        event_generation == 0U ||
        event->type != SM64_SATURN_SEQUENCE_VM_EVENT_NOTE ||
        event->signed_value < -96 || event->signed_value > 127 ||
        binding->package_generation != engine->active_package_generation) {
        if (engine != 0) engine->malformed_events++;
        return false;
    }
    duration = (uint8_t)event->arg1;
    /* Source layer decay starts when remaining delay reaches
     * noteDuration * playPercentage / 256 (seqplayer.c:760-764). */
    lifetime = event->arg0 - (mul_u16(event->arg0, duration) >> 8);
    if (lifetime == 0U) lifetime = 1U;
    if (lifetime > 0xffffU) lifetime = 0xffffU;
    request.package_generation = binding->package_generation;
    request.note_id = ((event_generation & 0xffffU) << 16) | event->source_offset;
    if (request.note_id == 0U) request.note_id = 1U;
    request.sound_ram_offset = binding->sound_ram_offset;
    request.sample_count = binding->sample_count;
    request.loop_start = binding->loop_start;
    request.sample_rate = binding->sample_rate;
    request.tuning_q12 = binding->tuning_q12;
    request.sustain_q15 = binding->sustain_q15;
    request.lifetime_ticks = (uint16_t)lifetime;
    request.root_note = binding->root_note;
    request.note = event->signed_value;
    request.pan = binding->pan;
    request.velocity = (uint8_t)(event->arg1 >> 8);
    request.priority = binding->priority;
    request.source_class = binding->source_class;
    request.loop = binding->loop;
    request.attack_ticks = binding->attack_ticks;
    request.decay_ticks = binding->decay_ticks;
    request.release_ticks = binding->release_ticks;
    if (!sm64_saturn_voice_allocator_start(&engine->allocator, &request,
                                            allocation)) {
        if (!sm64_saturn_desired_voice_request_valid(&request))
            engine->malformed_events++;
        return false;
    }
    return true;
}

bool sm64_saturn_audio_engine_service_generation(
    sm64_saturn_audio_engine_t *engine, uint32_t generation)
{
    if (engine == 0 || generation == 0U ||
        (engine->has_service_generation &&
         !generation_newer(generation, engine->last_service_generation))) {
        if (engine != 0) engine->duplicate_generations++;
        return false;
    }
    engine->has_service_generation = true;
    engine->last_service_generation = generation;
    sm64_saturn_voice_allocator_tick(&engine->allocator);
    return true;
}
