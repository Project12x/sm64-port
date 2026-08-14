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

/* Keep target diagnostics precise without weakening the shared validator. */
static uint16_t request_invalid_reason(
    const sm64_saturn_voice_request_t *request)
{
    uint16_t reason = 0U;
    uint32_t end;
    if (request == 0) return 0x0001U;
    if (request->package_generation == 0U) reason |= 0x0002U;
    if (request->note_id == 0U) reason |= 0x0004U;
    if (request->sample_count == 0U) reason |= 0x0008U;
    if (request->sample_rate == 0U || request->sample_rate > 44100U)
        reason |= 0x0010U;
    if (request->tuning_q12 < 1024U || request->tuning_q12 > 16384U)
        reason |= 0x0020U;
    if (request->velocity > 127U) reason |= 0x0040U;
    if (request->sustain_q15 > 0x7fffU) reason |= 0x0080U;
    if (request->source_class < SM64_SATURN_VOICE_CLASS_MUSIC ||
        request->source_class > SM64_SATURN_VOICE_CLASS_SFX)
        reason |= 0x0100U;
    if (request->attack_ticks == 0U || request->decay_ticks == 0U ||
        request->release_ticks == 0U)
        reason |= 0x0200U;
    if (request->root_note < -96 || request->root_note > 127 ||
        request->note < -96 || request->note > 127)
        reason |= 0x0400U;
    end = request->sound_ram_offset + request->sample_count;
    if (end < request->sound_ram_offset ||
        request->sound_ram_offset >= SM64_SATURN_SOUND_RAM_LIMIT ||
        end > SM64_SATURN_SOUND_RAM_LIMIT)
        reason |= 0x0800U;
    if (request->loop != 0U && request->loop_start >= request->sample_count)
        reason |= 0x1000U;
    return reason;
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
    if (engine == 0 || event == 0 || binding == 0 || allocation == 0) {
        if (engine != 0) engine->last_failure = 0x0001U;
        if (engine != 0) engine->malformed_events++;
        return false;
    }
    if (event_generation == 0U ||
        event->type != SM64_SATURN_SEQUENCE_VM_EVENT_NOTE) {
        engine->last_failure = 0x0002U;
        engine->malformed_events++;
        return false;
    }
    if (event->signed_value < -96 || event->signed_value > 127) {
        engine->last_failure = 0x0004U;
        engine->malformed_events++;
        return false;
    }
    if (binding->package_generation != engine->active_package_generation) {
        engine->last_failure = 0x0008U;
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
    if (!sm64_saturn_desired_voice_request_valid(&request)) {
        engine->last_failure = (uint16_t)(0x0100U |
                                          request_invalid_reason(&request));
        engine->malformed_events++;
        return false;
    }
    if (!sm64_saturn_voice_allocator_start(&engine->allocator, &request,
                                            allocation)) {
        engine->last_failure = engine->allocator.dropped_music != 0U
            ? 0x0020U : 0x0040U;
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
