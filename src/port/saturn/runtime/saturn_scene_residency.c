#include "saturn_scene_residency.h"

#include <stddef.h>
#include <string.h>

#include "../gfx/saturn_render_snapshot.h"

static bool id_equal(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32U) == 0;
}

static uint32_t feature_for_kind(uint16_t kind)
{
    if (kind == SM64_SATURN_SCENE_ACTOR_DEPENDENCIES)
        return SM64_SATURN_SCENE_FEATURE_DYNAMIC_ACTOR_CLOSURE;
    if (kind == SM64_SATURN_SCENE_ANIMATION_DEPENDENCIES)
        return SM64_SATURN_SCENE_FEATURE_COMPLETE_MARIO_ANIMATION;
    if (kind == SM64_SATURN_SCENE_AUDIO_DEPENDENCIES)
        return SM64_SATURN_SCENE_FEATURE_SEMANTIC_AUDIO;
    return 0U;
}

static uint32_t required_dependencies(const sm64_saturn_scene_residency_t *state)
{
    uint32_t required = 0U, before, index;
    for (index = 0U; index < state->staging_view.dependency_count; index++)
        if ((state->active_feature_mask & feature_for_kind(
                state->staging_view.dependencies[index].payload_kind)) != 0U)
            required |= 1U << index;
    do {
        before = required;
        for (index = 0U; index < state->staging_view.dependency_count; index++)
            if ((required & (1U << index)) != 0U)
                required |= state->staging_view.dependencies[index].dependency_mask;
    } while (before != required);
    return required;
}

static const sm64_saturn_scene_payload_source_t *find_payload(
    const sm64_saturn_scene_residency_t *state, const uint8_t stable_id[32])
{
    uint32_t index;
    for (index = 0U; index < state->payload_count; index++)
        if (id_equal(state->payloads[index].stable_id, stable_id)) return &state->payloads[index];
    return NULL;
}

static bool payloads_valid(const sm64_saturn_scene_residency_t *state,
                           const sm64_saturn_scene_package_view_t *view)
{
    uint8_t digest[32];
    uint32_t index;
    if (state->payload_count != view->dependency_count) return false;
    for (index = 0U; index < view->dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *dependency = &view->dependencies[index];
        const sm64_saturn_scene_payload_source_t *source = find_payload(state, dependency->stable_id);
        if (source == NULL || source->bytes == NULL || source->byte_count != dependency->byte_count ||
            source->generation != dependency->generation) return false;
        sm64_saturn_scene_package_sha256(source->bytes, source->byte_count, digest);
        if (!id_equal(digest, dependency->content_sha256)) return false;
    }
    return true;
}

static bool add_bytes(uint32_t *total, uint32_t bytes)
{
    if (bytes > UINT32_MAX - *total) return false;
    *total += bytes;
    return true;
}

static bool add_placement(uint32_t *total, uint32_t bytes, uint32_t scratch,
                          uint32_t alignment)
{
    uint32_t aligned;
    if (alignment == 0U || *total > UINT32_MAX - (alignment - 1U)) return false;
    aligned = (*total + alignment - 1U) & ~(alignment - 1U);
    *total = aligned;
    return add_bytes(total, bytes) && add_bytes(total, scratch);
}

static bool placement_fits(const sm64_saturn_scene_residency_t *state,
                           sm64_saturn_scene_resident_identity_t *identity)
{
    uint32_t used[SM64_SATURN_SCENE_DESTINATION_COUNT] = {0};
    uint32_t required = required_dependencies(state), slot, index;
    memset(identity->destination_bytes, 0, sizeof(identity->destination_bytes));
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed)
        for (index = 1U; index < SM64_SATURN_SCENE_DESTINATION_COUNT; index++)
            if (!add_bytes(&used[index], state->slot[slot].destination_bytes[index])) return false;
    for (index = 0U; index < state->staging_view.section_count; index++) {
        const sm64_saturn_scene_section_view_t *section = &state->staging_view.sections[index];
        if (section->destination_class != SM64_SATURN_SCENE_DESTINATION_NONE &&
            !add_placement(&identity->destination_bytes[section->destination_class], section->byte_size,
                           section->maximum_scratch, section->alignment)) return false;
    }
    for (index = 0U; index < state->staging_view.dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *dependency = &state->staging_view.dependencies[index];
        if ((required & (1U << index)) != 0U &&
            !add_placement(&identity->destination_bytes[dependency->destination_class], dependency->byte_count,
                           dependency->maximum_scratch, dependency->alignment)) return false;
    }
    for (index = 1U; index < SM64_SATURN_SCENE_DESTINATION_COUNT; index++) {
        if (!add_bytes(&used[index], identity->destination_bytes[index]) || used[index] > state->capacity[index]) return false;
    }
    return true;
}

static void aggregate_identity(const sm64_saturn_scene_package_view_t *view,
                               uint16_t kind, uint8_t output[32])
{
    (void)sm64_saturn_scene_package_identity_for_kind(view, kind, output);
}

void sm64_saturn_scene_residency_reset(
    sm64_saturn_scene_residency_t *state, uint32_t active_feature_mask,
    const uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT])
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->staging_slot = -1;
    state->active_feature_mask = active_feature_mask &
        (SM64_SATURN_SCENE_FEATURE_COMPLETE_MARIO_ANIMATION |
         SM64_SATURN_SCENE_FEATURE_DYNAMIC_ACTOR_CLOSURE |
         SM64_SATURN_SCENE_FEATURE_SEMANTIC_AUDIO);
    if (capacity != NULL) memcpy(state->capacity, capacity, sizeof(state->capacity));
}

bool sm64_saturn_scene_residency_bind_payloads(
    sm64_saturn_scene_residency_t *state,
    const sm64_saturn_scene_payload_source_t *payloads, uint16_t payload_count)
{
    uint32_t index, earlier;
    if (state == NULL || state->staging_generation != 0U ||
        payload_count > SM64_SATURN_SCENE_MAX_DEPENDENCIES ||
        (payload_count != 0U && payloads == NULL)) return false;
    for (index = 0U; index < payload_count; index++) {
        if (payloads[index].bytes == NULL || payloads[index].byte_count == 0U || payloads[index].generation == 0U ||
            payloads[index].stable_id[0] == 0U) return false;
        for (earlier = 0U; earlier < index; earlier++)
            if (id_equal(payloads[index].stable_id, payloads[earlier].stable_id)) return false;
    }
    memset(state->payloads, 0, sizeof(state->payloads));
    if (payload_count != 0U) memcpy(state->payloads, payloads, payload_count * sizeof(*payloads));
    state->payload_count = payload_count;
    return true;
}

bool sm64_saturn_scene_residency_begin(sm64_saturn_scene_residency_t *state,
                                       const sm64_saturn_scene_package_view_t *view,
                                       uint32_t generation)
{
    uint32_t slot;
    sm64_saturn_scene_resident_identity_t identity;
    if (state == NULL || view == NULL || generation == 0U || state->staging_generation != 0U ||
        generation == state->active_generation || view->root_bytes == NULL || !payloads_valid(state, view)) return false;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].generation == generation) return false;
    for (slot = 0U; slot < 2U; slot++) if (!state->slot[slot].committed && state->slot[slot].generation == 0U) break;
    if (slot == 2U) return false;
    state->staging_view = *view;
    memset(&identity, 0, sizeof(identity));
    if (!placement_fits(state, &identity)) { memset(&state->staging_view,0,sizeof(state->staging_view)); return false; }
    identity.generation = generation;
    identity.scene_package_id = ((uint32_t)view->package_sha256[0] << 24) |
        ((uint32_t)view->package_sha256[1] << 16) | ((uint32_t)view->package_sha256[2] << 8) | view->package_sha256[3];
    identity.active_feature_mask = state->active_feature_mask;
    memcpy(identity.package_sha256, view->package_sha256, 32U);
    memcpy(identity.dependency_set_sha256, view->dependency_set_sha256, 32U);
    aggregate_identity(view, SM64_SATURN_SCENE_ACTOR_DEPENDENCIES, identity.actor_bank_identity);
    aggregate_identity(view, SM64_SATURN_SCENE_ANIMATION_DEPENDENCIES, identity.animation_bank_identity);
    aggregate_identity(view, SM64_SATURN_SCENE_AUDIO_DEPENDENCIES, identity.audio_bank_identity);
    state->slot[slot] = identity; state->staging_slot = (int8_t)slot; state->staging_generation = generation;
    state->loaded_section_mask = 0U; state->loaded_dependency_mask = 0U;
    return true;
}

bool sm64_saturn_scene_residency_load_section(sm64_saturn_scene_residency_t *state,
                                              uint16_t section_index)
{
    uint32_t index, local_loaded, required;
    const sm64_saturn_scene_section_view_t *section;
    bool progress;
    if (state == NULL || state->staging_generation == 0U || section_index >= state->staging_view.section_count ||
        (state->loaded_section_mask & (1U << section_index)) != 0U) return false;
    section = &state->staging_view.sections[section_index];
    required = required_dependencies(state); local_loaded = state->loaded_dependency_mask;
    if ((section->dependency_mask & ~state->loaded_section_mask) != 0U) return false;
    do {
        progress = false;
        for (index = 0U; index < state->staging_view.dependency_count; index++) {
            const sm64_saturn_scene_dependency_view_t *dependency = &state->staging_view.dependencies[index];
            if (dependency->payload_kind != section->kind || (required & (1U << index)) == 0U ||
                (local_loaded & (1U << index)) != 0U || (dependency->dependency_mask & ~local_loaded) != 0U) continue;
            local_loaded |= 1U << index; progress = true;
        }
    } while (progress);
    for (index = 0U; index < state->staging_view.dependency_count; index++)
        if (state->staging_view.dependencies[index].payload_kind == section->kind &&
            (required & (1U << index)) != 0U && (local_loaded & (1U << index)) == 0U) return false;
    state->loaded_dependency_mask = local_loaded;
    state->loaded_section_mask |= 1U << section_index;
    return true;
}

static void rollback_staging(sm64_saturn_scene_residency_t *state)
{
    if (state->staging_slot >= 0) memset(&state->slot[(uint8_t)state->staging_slot], 0, sizeof(state->slot[0]));
    memset(&state->staging_view, 0, sizeof(state->staging_view));
    state->staging_generation = 0U; state->loaded_section_mask = 0U; state->loaded_dependency_mask = 0U;
    state->staging_slot = -1; state->quarantine_count++;
}

bool sm64_saturn_scene_residency_commit(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation)
{
    uint32_t expected_sections, required, pending;
    sm64_saturn_scene_resident_identity_t *identity;
    if (state == NULL || generation == 0U || generation != state->staging_generation || state->staging_slot < 0) return false;
    expected_sections = state->staging_view.section_count == 32U ? 0xFFFFFFFFU :
        ((1U << state->staging_view.section_count) - 1U);
    required = required_dependencies(state);
    if (state->loaded_section_mask != expected_sections || (state->loaded_dependency_mask & required) != required) {
        rollback_staging(state); return false;
    }
    identity = &state->slot[(uint8_t)state->staging_slot];
    pending = SM64_SATURN_SCENE_RETIRE_RENDER;
    if (required != 0U) pending |= SM64_SATURN_SCENE_RETIRE_BANK;
    for (uint32_t index = 0U; index < state->staging_view.dependency_count; index++)
        if ((required & (1U << index)) != 0U && state->staging_view.dependencies[index].payload_kind == SM64_SATURN_SCENE_AUDIO_DEPENDENCIES)
            pending |= SM64_SATURN_SCENE_RETIRE_VOICE;
    identity->retirement_pending = pending; identity->committed = 1U;
    state->active_generation = generation;
    memset(&state->staging_view, 0, sizeof(state->staging_view));
    state->staging_generation = 0U; state->loaded_section_mask = 0U; state->loaded_dependency_mask = 0U; state->staging_slot = -1;
    return true;
}

bool sm64_saturn_scene_residency_mark_retired(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer_mask)
{
    uint32_t slot;
    if (state == NULL || generation == 0U || consumer_mask == 0U ||
        (consumer_mask & ~(SM64_SATURN_SCENE_RETIRE_RENDER|SM64_SATURN_SCENE_RETIRE_BANK|SM64_SATURN_SCENE_RETIRE_VOICE)) != 0U) return false;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed && state->slot[slot].generation == generation) {
        if ((state->slot[slot].retirement_pending & consumer_mask) != consumer_mask) return false;
        state->slot[slot].retirement_pending &= ~consumer_mask; return true;
    }
    return false;
}

bool sm64_saturn_scene_residency_unload(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation)
{
    uint32_t slot;
    if (state == NULL || generation == 0U || generation == state->active_generation) return false;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed && state->slot[slot].generation == generation) {
        if (state->slot[slot].retirement_pending != 0U) return false;
        memset(&state->slot[slot], 0, sizeof(state->slot[slot])); return true;
    }
    return false;
}

const sm64_saturn_scene_resident_identity_t *
sm64_saturn_scene_residency_active(const sm64_saturn_scene_residency_t *state)
{
    uint32_t slot;
    if (state == NULL || state->active_generation == 0U) return NULL;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed &&
        state->slot[slot].generation == state->active_generation) return &state->slot[slot];
    return NULL;
}

bool sm64_saturn_scene_residency_snapshot_apply(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    struct sm64_saturn_render_snapshot *snapshot)
{
    const sm64_saturn_scene_resident_identity_t *identity = sm64_saturn_scene_residency_active(state);
    if (identity == NULL || snapshot == NULL || identity->generation != generation || snapshot->generation != generation) return false;
    snapshot->scene_package_id = identity->scene_package_id;
    snapshot->active_feature_mask = identity->active_feature_mask;
    memcpy(snapshot->scene_package_sha256, identity->package_sha256, 32U);
    memcpy(snapshot->scene_dependency_set_sha256, identity->dependency_set_sha256, 32U);
    memcpy(snapshot->actor_bank_identity, identity->actor_bank_identity, 32U);
    memcpy(snapshot->animation_bank_identity, identity->animation_bank_identity, 32U);
    memcpy(snapshot->audio_bank_identity, identity->audio_bank_identity, 32U);
    return true;
}
