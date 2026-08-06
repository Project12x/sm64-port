/* Priority/age policy is a bounded scalar close-port of the allocation order
 * in Project12x/sm64-port@36d015fb playback.c:1199-1372. Music protection and
 * deterministic SFX drop telemetry are Saturn-specific policy additions. */
#include "voice_allocator.h"

void sm64_saturn_voice_allocator_init(sm64_saturn_voice_allocator_t *allocator)
{
    if (allocator != 0) *allocator = (sm64_saturn_voice_allocator_t){0};
}

static int16_t choose_victim(const sm64_saturn_voice_allocator_t *allocator,
                             const sm64_saturn_voice_request_t *request)
{
    int16_t victim = -1;
    uint8_t i;
    for (i = 0U; i < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++i) {
        const sm64_saturn_voice_entry_t *entry = &allocator->voices[i];
        if (!entry->desired.active ||
            entry->request.source_class != SM64_SATURN_VOICE_CLASS_SFX)
            continue;
        if (request->source_class == SM64_SATURN_VOICE_CLASS_SFX &&
            entry->request.priority > request->priority)
            continue;
        if (victim < 0 ||
            entry->request.priority < allocator->voices[victim].request.priority ||
            (entry->request.priority == allocator->voices[victim].request.priority &&
             entry->desired.age < allocator->voices[victim].desired.age))
            victim = (int16_t)i;
    }
    return victim;
}

bool sm64_saturn_voice_allocator_start(
    sm64_saturn_voice_allocator_t *allocator,
    const sm64_saturn_voice_request_t *request,
    sm64_saturn_voice_allocation_t *allocation)
{
    sm64_saturn_desired_voice_t built;
    int16_t selected = -1;
    uint8_t i;
    if (allocation != 0) *allocation = (sm64_saturn_voice_allocation_t){0};
    if (allocator == 0 || request == 0 || allocation == 0 ||
        !sm64_saturn_desired_voice_request_valid(request))
        return false;
    for (i = 0U; i < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++i) {
        if (!allocator->voices[i].desired.active) {
            selected = (int16_t)i;
            break;
        }
    }
    if (selected < 0) selected = choose_victim(allocator, request);
    if (selected < 0) {
        if (request->source_class == SM64_SATURN_VOICE_CLASS_SFX)
            allocator->dropped_sfx++;
        else
            allocator->dropped_music++;
        return false;
    }
    allocator->next_age++;
    if (!sm64_saturn_desired_voice_begin(&built, request, (uint8_t)selected,
                                          allocator->next_age))
        return false;
    if (allocator->voices[selected].desired.active) {
        allocation->stolen = true;
        allocation->displaced_note_id = allocator->voices[selected].request.note_id;
        allocator->steals++;
    }
    allocator->voices[selected].request = *request;
    allocator->voices[selected].desired = built;
    allocator->voices[selected].lifetime_remaining = request->lifetime_ticks;
    allocation->voice_index = (uint8_t)selected;
    return true;
}

bool sm64_saturn_voice_allocator_release(
    sm64_saturn_voice_allocator_t *allocator, uint32_t note_id)
{
    uint8_t i;
    if (allocator == 0 || note_id == 0U) return false;
    for (i = 0U; i < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++i) {
        if (allocator->voices[i].desired.active &&
            allocator->voices[i].request.note_id == note_id) {
            if (!sm64_saturn_desired_voice_release(&allocator->voices[i].desired))
                return false;
            allocator->voices[i].lifetime_remaining = 0U;
            allocator->releases++;
            return true;
        }
    }
    return false;
}

void sm64_saturn_voice_allocator_tick(sm64_saturn_voice_allocator_t *allocator)
{
    uint8_t i;
    if (allocator == 0) return;
    for (i = 0U; i < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++i) {
        sm64_saturn_voice_entry_t *entry = &allocator->voices[i];
        if (!entry->desired.active) continue;
        if (!entry->desired.releasing && entry->lifetime_remaining != 0U) {
            entry->lifetime_remaining--;
            if (entry->lifetime_remaining == 0U) {
                (void)sm64_saturn_desired_voice_release(&entry->desired);
                allocator->releases++;
            }
        }
        sm64_saturn_desired_voice_tick(&entry->desired);
    }
}

uint8_t sm64_saturn_voice_allocator_active_count(
    const sm64_saturn_voice_allocator_t *allocator)
{
    uint8_t i, count = 0U;
    if (allocator == 0) return 0U;
    for (i = 0U; i < SM64_SATURN_SEMANTIC_VOICE_COUNT; ++i)
        if (allocator->voices[i].desired.active) count++;
    return count;
}
