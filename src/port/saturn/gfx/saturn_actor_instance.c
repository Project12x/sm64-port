#include "saturn_actor_instance.h"
#include "saturn_render_cluster.h"

#include <string.h>

#if defined(__sh__)
#include <cpu/cache.h>
#endif

static sm64_saturn_geo_state_observer_t *s_bound_observer;

static inline void actor_bank_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static inline sm64_saturn_actor_instance_bank_t *actor_bank_uncached(
    sm64_saturn_actor_instance_bank_t *bank)
{
    if (bank == NULL) return NULL;
#if defined(__sh__)
    return (sm64_saturn_actor_instance_bank_t *)(CPU_CACHE_THROUGH |
                                                  (uintptr_t)bank);
#else
    return bank;
#endif
}

static bool actor_generation_after(uint32_t candidate, uint32_t reference)
{
    if (candidate == 0U) return false;
    if (reference == 0U) return true;
    if (candidate == reference) return false;
    return candidate == sm64_saturn_render_generation_next(reference) ||
           (uint32_t)(candidate - reference) < 0x80000000U;
}

static uint32_t fold_hash(uint32_t hash, uint32_t value)
{
    hash ^= value + 0x9e3779b9U + (hash << 6) + (hash >> 2);
    return hash;
}

static bool nonzero_hash(const uint32_t hash[8])
{
    uint16_t i;
    uint32_t value = 0U;
    for (i = 0U; i < 8U; i++) value |= hash[i];
    return value != 0U;
}

static bool valid_observation(const sm64_saturn_actor_source_observation_t *source,
                              uint32_t generation,
                              sm64_saturn_actor_capture_telemetry_t *stats)
{
    uint16_t i;
    if (source == NULL) return false;
    if (source->source_generation != generation || generation == 0U) {
        if (stats != NULL) stats->stale_generation_count++;
        return false;
    }
    if (source->family_id == 0U || source->actor_bank_id == 0U ||
        !nonzero_hash(source->actor_bank_hash_words)) {
        if (stats != NULL) stats->unknown_family_count++;
        return false;
    }
    if (source->active == 0U || source->reserved != 0U ||
        source->scene_package_generation == 0U ||
        source->switch_count > SM64_SATURN_ACTOR_MAX_SWITCHES ||
        source->render_range_min_q16 > source->render_range_max_q16 ||
        source->draw_distance_q16 < 0 || source->opacity > 255U ||
        source->parent_index == source->pool_slot ||
        (source->parent_index != SM64_SATURN_ACTOR_INSTANCE_NO_PARENT &&
         source->parent_index >= SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE)) {
        if (stats != NULL) stats->malformed_count++;
        return false;
    }
    for (i = 0U; i < 3U; i++) {
        if (source->scale_q16[i] <= 0) {
            if (stats != NULL) stats->malformed_count++;
            return false;
        }
    }
    return true;
}

void sm64_saturn_actor_instances_set_observer(
    sm64_saturn_geo_state_observer_t *observer)
{
    s_bound_observer = observer;
}

bool sm64_saturn_actor_instances_capture(
    sm64_saturn_actor_instance_snapshot_t *out, uint16_t capacity,
    uint32_t generation, uint16_t *count,
    sm64_saturn_actor_capture_telemetry_t *stats)
{
    uint16_t i, accepted = 0U;
    uint32_t identity_hash = 0x811c9dc5U;
    uint32_t generation_hash = 0x811c9dc5U;
    sm64_saturn_geo_state_observer_t *observer = s_bound_observer;
    if (count != NULL) *count = 0U;
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (out == NULL || count == NULL || observer == NULL || capacity == 0U ||
        capacity > SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE || generation == 0U)
        return false;
    if (observer->overflow_latched != 0U || observer->count > capacity) {
        if (stats != NULL) stats->capacity_overflow_count++;
        return false;
    }
    if (stats != NULL) stats->observed_count = observer->count;
    for (i = 0U; i < observer->count; i++) {
        const sm64_saturn_actor_source_observation_t *source =
            &observer->observations[i];
        sm64_saturn_actor_instance_snapshot_t *destination;
        uint16_t word;
        if (source->pool_slot >= observer->capacity ||
            source->pool_slot >= SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE) {
            if (stats != NULL) stats->malformed_count++;
            continue;
        }
        if (!valid_observation(source, generation, stats)) continue;
        destination = &out[accepted];
        memset(destination, 0, sizeof(*destination));
        destination->generation = generation;
        destination->scene_package_generation = source->scene_package_generation;
        destination->instance_key = ((uint32_t)observer->incarnation[source->pool_slot] << 16) |
                                    source->pool_slot;
        destination->actor_bank_id = source->actor_bank_id;
        for (word = 0U; word < 8U; word++)
            destination->actor_bank_hash_words[word] = source->actor_bank_hash_words[word];
        destination->family_id = source->family_id;
        destination->model_id = source->model_id;
        destination->parent_index = source->parent_index;
        destination->parent_node_ordinal = source->parent_node_ordinal;
        destination->feature_state = source->feature_state;
        memcpy(destination->position_q16, source->position_q16, sizeof(destination->position_q16));
        memcpy(destination->scale_q16, source->scale_q16, sizeof(destination->scale_q16));
        memcpy(destination->held_offset_q16, source->held_offset_q16, sizeof(destination->held_offset_q16));
        destination->draw_distance_q16 = source->draw_distance_q16;
        destination->render_range_min_q16 = source->render_range_min_q16;
        destination->render_range_max_q16 = source->render_range_max_q16;
        memcpy(destination->angle, source->angle, sizeof(destination->angle));
        destination->animation_id = source->animation_id;
        destination->animation_frame = source->animation_frame;
        destination->animation_accel = source->animation_accel;
        destination->anim_state = source->anim_state;
        memcpy(destination->effect_params_q16, source->effect_params_q16, sizeof(destination->effect_params_q16));
        destination->effect_lifetime = source->effect_lifetime;
        destination->opacity = source->opacity;
        destination->render_range_state = source->render_range_state;
        destination->billboard_state = source->billboard_state;
        destination->shadow_type = source->shadow_type;
        destination->shadow_scale = source->shadow_scale;
        destination->shadow_solidity = source->shadow_solidity;
        destination->effect_kind = source->effect_kind;
        destination->effect_flags = source->effect_flags;
        destination->area_index = source->area_index;
        destination->active = source->active;
        destination->render_active = source->render_active;
        destination->switch_count = source->switch_count;
        memcpy(destination->switch_state, source->switch_state, sizeof(destination->switch_state));
        identity_hash = fold_hash(identity_hash, destination->instance_key);
        identity_hash = fold_hash(identity_hash, destination->family_id);
        generation_hash = fold_hash(generation_hash, destination->generation);
        generation_hash = fold_hash(generation_hash, destination->scene_package_generation);
        accepted++;
    }
    if (stats != NULL) {
        stats->published_count = accepted;
        stats->rejected_count = (uint16_t)(observer->count - accepted);
        stats->pool_slot_overflow_count = observer->pool_slot_overflow_count;
        stats->despawned_count = observer->despawned_count;
        stats->pool_reuse_count = observer->pool_reuse_count;
        stats->identity_hash = identity_hash;
        stats->generation_hash = generation_hash;
    }
    *count = accepted;
    return true;
}

void sm64_saturn_actor_instance_bank_init(
    sm64_saturn_actor_instance_bank_t *bank)
{
    if (bank == NULL) return;
    memset(bank, 0, sizeof(*bank));
    bank->state[0] = SM64_SATURN_ACTOR_INSTANCE_BANK_FREE;
    bank->state[1] = SM64_SATURN_ACTOR_INSTANCE_BANK_FREE;
    actor_bank_fence();
}

bool sm64_saturn_actor_instance_bank_begin_write(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation,
    uint8_t *index)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    uint8_t i;
    if (index != NULL) *index = 0xffU;
    if (shared == NULL || index == NULL || generation == 0U ||
        !actor_generation_after(generation, shared->last_published_generation))
        return false;
    for (i = 0U; i < 2U; i++) {
        if (shared->state[i] != SM64_SATURN_ACTOR_INSTANCE_BANK_FREE &&
            shared->generation[i] == generation)
            return false;
    }
    for (i = 0U; i < 2U; i++) {
        if (shared->state[i] != SM64_SATURN_ACTOR_INSTANCE_BANK_FREE) continue;
        shared->state[i] = SM64_SATURN_ACTOR_INSTANCE_BANK_WRITING;
        shared->generation[i] = generation;
        shared->count[i] = 0U;
        actor_bank_fence();
        *index = i;
        return true;
    }
    return false;
}

bool sm64_saturn_actor_instance_bank_publish(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index, uint16_t count,
    uint32_t generation)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    if (shared == NULL || index >= 2U ||
        count > SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE ||
        shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_WRITING ||
        shared->generation[index] != generation || generation == 0U ||
        !actor_generation_after(generation, shared->last_published_generation))
        return false;
    if ((index == 0U && shared->state[1] != SM64_SATURN_ACTOR_INSTANCE_BANK_FREE &&
         shared->generation[1] == generation) ||
        (index == 1U && shared->state[0] != SM64_SATURN_ACTOR_INSTANCE_BANK_FREE &&
         shared->generation[0] == generation))
        return false;
    shared->count[index] = count;
    actor_bank_fence();
    shared->state[index] = SM64_SATURN_ACTOR_INSTANCE_BANK_READY;
    shared->last_published_generation = generation;
    actor_bank_fence();
    return true;
}

bool sm64_saturn_actor_instance_bank_capture(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation,
    uint16_t capacity, uint8_t *index, uint16_t *count,
    sm64_saturn_actor_capture_telemetry_t *stats)
{
    uint8_t selected = 0xffU;
    uint16_t captured = 0U;
    if (count != NULL) *count = 0U;
    if (index != NULL) *index = 0xffU;
    if (bank == NULL || index == NULL || count == NULL ||
        !sm64_saturn_actor_instance_bank_begin_write(bank, generation,
                                                     &selected))
        return false;
    if (!sm64_saturn_actor_instances_capture(
            bank->snapshots[selected], capacity, generation, &captured, stats) ||
        !sm64_saturn_actor_instance_bank_publish(
            bank, selected, captured, generation)) {
        (void)sm64_saturn_actor_instance_bank_quarantine(bank, generation);
        return false;
    }
    *index = selected;
    *count = captured;
    return true;
}

const sm64_saturn_actor_instance_snapshot_t *
sm64_saturn_actor_instance_bank_acquire(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index,
    uint32_t generation, uint16_t *count)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    if (count != NULL) *count = 0U;
    if (shared == NULL || index >= 2U || count == NULL ||
        shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_READY ||
        shared->generation[index] != generation || generation == 0U)
        return NULL;
    actor_bank_fence();
    *count = shared->count[index];
    shared->active_index = index;
    shared->state[index] = SM64_SATURN_ACTOR_INSTANCE_BANK_RENDERING;
    actor_bank_fence();
    return shared->snapshots[index];
}

bool sm64_saturn_actor_instance_bank_complete(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    if (shared == NULL || index >= 2U ||
        shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_RENDERING)
        return false;
    actor_bank_fence();
    shared->state[index] = SM64_SATURN_ACTOR_INSTANCE_BANK_COMPLETE;
    actor_bank_fence();
    return true;
}

bool sm64_saturn_actor_instance_bank_retire(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    if (shared == NULL || index >= 2U ||
        shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_COMPLETE)
        return false;
    memset(shared->snapshots[index], 0, sizeof(shared->snapshots[index]));
    shared->count[index] = 0U;
    shared->generation[index] = 0U;
    actor_bank_fence();
    shared->state[index] = SM64_SATURN_ACTOR_INSTANCE_BANK_FREE;
    actor_bank_fence();
    return true;
}

bool sm64_saturn_actor_instance_bank_quarantine(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation)
{
    sm64_saturn_actor_instance_bank_t *const shared =
        actor_bank_uncached(bank);
    uint8_t index;
    if (shared == NULL || generation == 0U) return false;
    for (index = 0U; index < 2U; index++) {
        if (shared->generation[index] == generation &&
            shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_FREE &&
            shared->state[index] != SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED) {
            actor_bank_fence();
            shared->state[index] = SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED;
            actor_bank_fence();
            return true;
        }
    }
    return false;
}
