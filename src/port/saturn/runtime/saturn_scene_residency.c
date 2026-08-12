#include "saturn_scene_residency.h"

#include <stddef.h>
#include <string.h>

#include "../gfx/saturn_render_snapshot.h"
#include "../gfx/saturn_vdp1_frame_bank.h"

static inline void actor_texture_publication_fence(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#endif
}

static void actor_texture_publication_invalidate(
    sm64_saturn_scene_residency_t *state)
{
    state->actor_texture_publication.committed = 0U;
    actor_texture_publication_fence();
    memset(&state->actor_texture_publication, 0,
           sizeof(state->actor_texture_publication));
    actor_texture_publication_fence();
}

static bool actor_texture_publication_empty(
    const sm64_saturn_actor_texture_publication_t *publication)
{
    uint16_t index;
    if (publication == NULL || publication->texture_bytes != 0U ||
        publication->clut_bytes != 0U || publication->generation != 0U ||
        publication->mapping_count != 0U || publication->committed != 0U ||
        publication->reserved != 0U)
        return false;
    for (index = 0U;
         index < SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY; index++) {
        const sm64_saturn_actor_texture_mapping_t *mapping =
            &publication->mappings[index];
        if (mapping->bank_id != 0U || mapping->texture_base_offset != 0U ||
            mapping->clut_base_index != 0U || mapping->tile_count != 0U ||
            mapping->generation != 0U)
            return false;
    }
    return true;
}

static bool bytes_equal(const uint8_t a[32], const uint8_t b[32])
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
        if (bytes_equal(state->payloads[index].stable_id, stable_id))
            return &state->payloads[index];
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
        if (source == NULL || source->byte_count != dependency->byte_count ||
            source->generation != dependency->generation ||
            (source->bytes == NULL && source->byte_count != 0U) ||
            !sm64_saturn_scene_package_sha256(source->bytes, source->byte_count, digest) ||
            !bytes_equal(digest, dependency->content_sha256)) return false;
    }
    return true;
}

static bool add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || b > UINT32_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool align_u32(uint32_t value, uint32_t alignment, uint32_t *out)
{
    if (alignment == 0U || value > UINT32_MAX - (alignment - 1U)) return false;
    *out = (value + alignment - 1U) & ~(alignment - 1U);
    return true;
}

static const sm64_saturn_scene_resident_identity_t *other_resident(
    const sm64_saturn_scene_residency_t *state, uint32_t slot)
{
    uint32_t index;
    for (index = 0U; index < 2U; index++)
        if (index != slot && state->slot[index].committed) return &state->slot[index];
    return NULL;
}

static uint32_t destination_max_alignment(
    const sm64_saturn_scene_residency_t *state, uint32_t destination,
    uint32_t required)
{
    uint32_t maximum = 1U, index;
    for (index = 0U; index < state->staging_view.section_count; index++) {
        const sm64_saturn_scene_section_view_t *item = &state->staging_view.sections[index];
        if (item->destination_class == destination && item->alignment > maximum)
            maximum = item->alignment;
    }
    for (index = 0U; index < state->staging_view.dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &state->staging_view.dependencies[index];
        if ((required & (1U << index)) != 0U && item->destination_class == destination &&
            item->alignment > maximum) maximum = item->alignment;
    }
    return maximum;
}

static bool layout_destination_at(
    const sm64_saturn_scene_residency_t *state,
    sm64_saturn_scene_resident_identity_t *identity, uint32_t destination,
    uint32_t required, uint32_t base, uint32_t *end_out)
{
    uint32_t cursor = base, index;
    for (index = 0U; index < state->staging_view.section_count; index++) {
        const sm64_saturn_scene_section_view_t *item = &state->staging_view.sections[index];
        if (item->destination_class != destination) continue;
        if (!align_u32(cursor, item->alignment, &cursor)) return false;
        identity->section_storage_offset[index] = cursor;
        if (!add_u32(cursor, item->byte_size, &cursor) ||
            !add_u32(cursor, item->maximum_scratch, &cursor)) return false;
    }
    for (index = 0U; index < state->staging_view.dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &state->staging_view.dependencies[index];
        if ((required & (1U << index)) == 0U || item->destination_class != destination) continue;
        if (!align_u32(cursor, item->alignment, &cursor)) return false;
        identity->dependency_storage_offset[index] = cursor;
        if (!add_u32(cursor, item->byte_count, &cursor) ||
            !add_u32(cursor, item->maximum_scratch, &cursor)) return false;
    }
    *end_out = cursor;
    return true;
}

static bool layout_destination(
    const sm64_saturn_scene_residency_t *state,
    sm64_saturn_scene_resident_identity_t *identity, uint32_t slot,
    uint32_t destination, uint32_t required)
{
    const sm64_saturn_scene_resident_identity_t *existing = other_resident(state, slot);
    uint32_t end, base = 0U, maximum_alignment;
    if (!layout_destination_at(state, identity, destination, required, 0U, &end)) return false;
    if (end == 0U) return true;
    if (state->destination_storage[destination] == NULL) return false;
    if (existing != NULL && existing->destination_bytes[destination] != 0U &&
        end > existing->destination_base[destination]) {
        maximum_alignment = destination_max_alignment(state, destination, required);
        if (!add_u32(existing->destination_base[destination],
                     existing->destination_bytes[destination], &base) ||
            !align_u32(base, maximum_alignment, &base) ||
            !layout_destination_at(state, identity, destination, required, base, &end)) return false;
    }
    if (end > state->capacity[destination]) return false;
    identity->destination_base[destination] = base;
    identity->destination_bytes[destination] = end - base;
    return true;
}

static bool placement_layout(const sm64_saturn_scene_residency_t *state,
                             sm64_saturn_scene_resident_identity_t *identity,
                             uint32_t slot)
{
    const sm64_saturn_scene_resident_identity_t *existing = other_resident(state, slot);
    uint32_t root_end, required = required_dependencies(state), destination, index;
    for (index = 0U; index < SM64_SATURN_SCENE_MAX_SECTIONS; index++)
        identity->section_storage_offset[index] = UINT32_MAX;
    for (index = 0U; index < SM64_SATURN_SCENE_MAX_DEPENDENCIES; index++)
        identity->dependency_storage_offset[index] = UINT32_MAX;
    if (state->root_storage == NULL || state->staging_view.package_size > state->root_storage_capacity)
        return false;
    identity->root_storage_offset = 0U;
    if (existing != NULL && existing->root_byte_count != 0U &&
        state->staging_view.package_size > existing->root_storage_offset) {
        if (!add_u32(existing->root_storage_offset, existing->root_byte_count,
                     &identity->root_storage_offset)) return false;
    }
    if (!add_u32(identity->root_storage_offset, state->staging_view.package_size,
                 &root_end) || root_end > state->root_storage_capacity) return false;
    identity->root_byte_count = state->staging_view.package_size;
    for (destination = 1U; destination < SM64_SATURN_SCENE_DESTINATION_COUNT; destination++)
        if (!layout_destination(state, identity, slot, destination, required)) return false;
    return true;
}

static void clear_identity_storage(sm64_saturn_scene_residency_t *state,
                                   const sm64_saturn_scene_resident_identity_t *identity)
{
    uint32_t destination;
    if (state->root_storage != NULL && identity->root_byte_count != 0U)
        memset(state->root_storage + identity->root_storage_offset, 0, identity->root_byte_count);
    for (destination = 1U; destination < SM64_SATURN_SCENE_DESTINATION_COUNT; destination++)
        if (state->destination_storage[destination] != NULL && identity->destination_bytes[destination] != 0U)
            memset(state->destination_storage[destination] + identity->destination_base[destination],
                   0, identity->destination_bytes[destination]);
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
        if ((payloads[index].bytes == NULL && payloads[index].byte_count != 0U) ||
            payloads[index].stable_id[0] == 0U) return false;
        for (earlier = 0U; earlier < index; earlier++)
            if (bytes_equal(payloads[index].stable_id, payloads[earlier].stable_id)) return false;
    }
    memset(state->payloads, 0, sizeof(state->payloads));
    if (payload_count != 0U) memcpy(state->payloads, payloads, payload_count * sizeof(*payloads));
    state->payload_count = payload_count;
    return true;
}

bool sm64_saturn_scene_residency_bind_storage(
    sm64_saturn_scene_residency_t *state, void *root_storage,
    uint32_t root_storage_capacity,
    void *const destination_storage[SM64_SATURN_SCENE_DESTINATION_COUNT])
{
    uintptr_t root_begin, root_end;
    uint32_t destination, other;
    if (state == NULL || state->staging_generation != 0U || state->active_generation != 0U ||
        root_storage == NULL || root_storage_capacity == 0U || destination_storage == NULL) return false;
    root_begin = (uintptr_t)root_storage;
    if (root_storage_capacity > UINTPTR_MAX - root_begin) return false;
    root_end = root_begin + root_storage_capacity;
    for (destination = 1U; destination < SM64_SATURN_SCENE_DESTINATION_COUNT; destination++) {
        uintptr_t begin, end;
        if (state->capacity[destination] != 0U && destination_storage[destination] == NULL) return false;
        if (state->capacity[destination] == 0U) continue;
        begin = (uintptr_t)destination_storage[destination];
        if (state->capacity[destination] > UINTPTR_MAX - begin) return false;
        end = begin + state->capacity[destination];
        if (begin < root_end && root_begin < end) return false;
        for (other = 1U; other < destination; other++) {
            uintptr_t other_begin, other_end;
            if (state->capacity[other] == 0U) continue;
            other_begin = (uintptr_t)destination_storage[other];
            other_end = other_begin + state->capacity[other];
            if (begin < other_end && other_begin < end) return false;
        }
    }
    state->root_storage = (uint8_t *)root_storage;
    state->root_storage_capacity = root_storage_capacity;
    for (destination = 0U; destination < SM64_SATURN_SCENE_DESTINATION_COUNT; destination++)
        state->destination_storage[destination] = (uint8_t *)destination_storage[destination];
    return true;
}

bool sm64_saturn_scene_residency_begin(sm64_saturn_scene_residency_t *state,
                                       const sm64_saturn_scene_package_view_t *view,
                                       uint32_t generation)
{
    uint32_t slot;
    sm64_saturn_scene_resident_identity_t identity;
    sm64_saturn_scene_package_view_t owned_view;
    if (state == NULL || view == NULL || generation == 0U || state->staging_generation != 0U ||
        generation == state->active_generation || view->root_bytes == NULL || !payloads_valid(state, view)) return false;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].generation == generation) return false;
    for (slot = 0U; slot < 2U; slot++) if (!state->slot[slot].committed && state->slot[slot].generation == 0U) break;
    if (slot == 2U) return false;
    state->staging_view = *view;
    memset(&identity, 0, sizeof(identity));
    if (!placement_layout(state, &identity, slot)) {
        memset(&state->staging_view, 0, sizeof(state->staging_view)); return false;
    }
    clear_identity_storage(state, &identity);
    memcpy(state->root_storage + identity.root_storage_offset, view->root_bytes, view->package_size);
    if (!sm64_saturn_scene_package_validate(
            state->root_storage + identity.root_storage_offset, view->package_size, &owned_view) ||
        !bytes_equal(owned_view.package_sha256, view->package_sha256)) {
        clear_identity_storage(state, &identity);
        memset(&state->staging_view, 0, sizeof(state->staging_view)); return false;
    }
    state->staging_view = owned_view;
    if (!payloads_valid(state, &state->staging_view)) {
        clear_identity_storage(state, &identity);
        memset(&state->staging_view, 0, sizeof(state->staging_view)); return false;
    }
    identity.generation = generation;
    identity.scene_package_id = ((uint32_t)view->package_sha256[0] << 24) |
        ((uint32_t)view->package_sha256[1] << 16) | ((uint32_t)view->package_sha256[2] << 8) | view->package_sha256[3];
    identity.active_feature_mask = state->active_feature_mask;
    memcpy(identity.package_sha256, view->package_sha256, 32U);
    memcpy(identity.dependency_set_sha256, view->dependency_set_sha256, 32U);
    aggregate_identity(view, SM64_SATURN_SCENE_ACTOR_DEPENDENCIES, identity.actor_bank_identity);
    aggregate_identity(view, SM64_SATURN_SCENE_ANIMATION_DEPENDENCIES, identity.animation_bank_identity);
    aggregate_identity(view, SM64_SATURN_SCENE_AUDIO_DEPENDENCIES, identity.audio_bank_identity);
    state->slot[slot] = identity;
    state->staging_slot = (int8_t)slot;
    state->staging_generation = generation;
    state->loaded_section_mask = 0U;
    state->loaded_dependency_mask = 0U;
    return true;
}

bool sm64_saturn_scene_residency_load_section(sm64_saturn_scene_residency_t *state,
                                              uint16_t section_index)
{
    uint8_t digest[32];
    uint32_t index, local_loaded, required;
    const sm64_saturn_scene_section_view_t *section;
    sm64_saturn_scene_resident_identity_t *identity;
    bool progress;
    if (state == NULL || state->staging_generation == 0U || section_index >= state->staging_view.section_count ||
        (state->loaded_section_mask & (1U << section_index)) != 0U || state->staging_slot < 0) return false;
    section = &state->staging_view.sections[section_index];
    identity = &state->slot[(uint8_t)state->staging_slot];
    required = required_dependencies(state);
    local_loaded = state->loaded_dependency_mask;
    if ((section->dependency_mask & ~state->loaded_section_mask) != 0U) return false;
    do {
        progress = false;
        for (index = 0U; index < state->staging_view.dependency_count; index++) {
            const sm64_saturn_scene_dependency_view_t *dependency = &state->staging_view.dependencies[index];
            const sm64_saturn_scene_payload_source_t *source;
            uint8_t *destination;
            if (dependency->payload_kind != section->kind || (required & (1U << index)) == 0U ||
                (local_loaded & (1U << index)) != 0U ||
                (dependency->dependency_mask & ~local_loaded) != 0U) continue;
            source = find_payload(state, dependency->stable_id);
            if (source == NULL || identity->dependency_storage_offset[index] == UINT32_MAX) return false;
            destination = state->destination_storage[dependency->destination_class] +
                identity->dependency_storage_offset[index];
            if (source->byte_count != 0U) memmove(destination, source->bytes, source->byte_count);
            if (!sm64_saturn_scene_package_sha256(destination, source->byte_count, digest) ||
                !bytes_equal(digest, dependency->content_sha256)) return false;
            local_loaded |= 1U << index;
            progress = true;
        }
    } while (progress);
    for (index = 0U; index < state->staging_view.dependency_count; index++)
        if (state->staging_view.dependencies[index].payload_kind == section->kind &&
            (required & (1U << index)) != 0U && (local_loaded & (1U << index)) == 0U) return false;
    if (section->destination_class != SM64_SATURN_SCENE_DESTINATION_NONE) {
        uint8_t *destination = state->destination_storage[section->destination_class] +
            identity->section_storage_offset[section_index];
        if (section->byte_size != 0U)
            memmove(destination, state->staging_view.root_bytes + section->offset, section->byte_size);
        if (!sm64_saturn_scene_package_sha256(destination, section->byte_size, digest) ||
            !bytes_equal(digest, section->content_sha256)) return false;
    }
    state->loaded_dependency_mask = local_loaded;
    state->loaded_section_mask |= 1U << section_index;
    return true;
}

static void rollback_staging(sm64_saturn_scene_residency_t *state)
{
    if (state->staging_generation != 0U &&
        state->actor_texture_publication.generation ==
            state->staging_generation)
        actor_texture_publication_invalidate(state);
    if (state->staging_slot >= 0) {
        clear_identity_storage(state, &state->slot[(uint8_t)state->staging_slot]);
        memset(&state->slot[(uint8_t)state->staging_slot], 0, sizeof(state->slot[0]));
    }
    memset(&state->staging_view, 0, sizeof(state->staging_view));
    state->staging_generation = 0U;
    state->loaded_section_mask = 0U;
    state->loaded_dependency_mask = 0U;
    state->staging_slot = -1;
    state->quarantine_count++;
}

static bool owned_bytes_valid(const sm64_saturn_scene_residency_t *state,
                              const sm64_saturn_scene_resident_identity_t *identity)
{
    sm64_saturn_scene_package_view_t view;
    uint8_t digest[32];
    uint32_t required = required_dependencies(state), index;
    if (!sm64_saturn_scene_package_validate(
            state->root_storage + identity->root_storage_offset,
            identity->root_byte_count, &view) ||
        !bytes_equal(view.package_sha256, identity->package_sha256)) return false;
    for (index = 0U; index < view.section_count; index++) {
        const sm64_saturn_scene_section_view_t *section = &view.sections[index];
        if (section->destination_class == SM64_SATURN_SCENE_DESTINATION_NONE) continue;
        if (identity->section_storage_offset[index] == UINT32_MAX ||
            !sm64_saturn_scene_package_sha256(
                state->destination_storage[section->destination_class] +
                    identity->section_storage_offset[index],
                section->byte_size, digest) || !bytes_equal(digest, section->content_sha256)) return false;
    }
    for (index = 0U; index < view.dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *dependency = &view.dependencies[index];
        if ((required & (1U << index)) == 0U) continue;
        if (identity->dependency_storage_offset[index] == UINT32_MAX ||
            !sm64_saturn_scene_package_sha256(
                state->destination_storage[dependency->destination_class] +
                    identity->dependency_storage_offset[index],
                dependency->byte_count, digest) || !bytes_equal(digest, dependency->content_sha256)) return false;
    }
    return true;
}

bool sm64_saturn_scene_residency_commit(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation)
{
    uint32_t expected_sections, required, slot;
    sm64_saturn_scene_resident_identity_t *identity;
    if (state == NULL || generation == 0U || generation != state->staging_generation || state->staging_slot < 0) return false;
    expected_sections = (1U << state->staging_view.section_count) - 1U;
    required = required_dependencies(state);
    identity = &state->slot[(uint8_t)state->staging_slot];
    if (state->loaded_section_mask != expected_sections ||
        (state->loaded_dependency_mask & required) != required || !owned_bytes_valid(state, identity)) {
        rollback_staging(state); return false;
    }
    for (slot = 0U; slot < 2U; slot++)
        if (state->slot[slot].committed && state->slot[slot].generation == state->active_generation)
            state->slot[slot].consumer_open_mask = 0U;
    identity->consumer_open_mask = SM64_SATURN_SCENE_RETIRE_RENDER |
        SM64_SATURN_SCENE_RETIRE_BANK;
    for (slot = 0U; slot < state->staging_view.dependency_count; slot++)
        if ((required & (1U << slot)) != 0U &&
            state->staging_view.dependencies[slot].payload_kind == SM64_SATURN_SCENE_AUDIO_DEPENDENCIES)
            identity->consumer_open_mask |= SM64_SATURN_SCENE_RETIRE_VOICE;
    identity->committed = 1U;
    if (state->actor_texture_publication.committed != 1U ||
        state->actor_texture_publication.generation != generation)
        actor_texture_publication_invalidate(state);
    state->active_generation = generation;
    memset(&state->staging_view, 0, sizeof(state->staging_view));
    state->staging_generation = 0U;
    state->loaded_section_mask = 0U;
    state->loaded_dependency_mask = 0U;
    state->staging_slot = -1;
    return true;
}

static sm64_saturn_scene_resident_identity_t *find_resident(
    sm64_saturn_scene_residency_t *state, uint32_t generation)
{
    uint32_t slot;
    if (state == NULL || generation == 0U) return NULL;
    for (slot = 0U; slot < 2U; slot++)
        if (state->slot[slot].committed && state->slot[slot].generation == generation)
            return &state->slot[slot];
    return NULL;
}

static int consumer_index(uint32_t consumer)
{
    if (consumer == SM64_SATURN_SCENE_RETIRE_RENDER) return 0;
    if (consumer == SM64_SATURN_SCENE_RETIRE_BANK) return 1;
    if (consumer == SM64_SATURN_SCENE_RETIRE_VOICE) return 2;
    return -1;
}

static uint32_t reference_token_from_handle(const void *handle,
                                            uint32_t token_namespace)
{
    const uint8_t *bytes = (const uint8_t *)&handle;
    uint32_t hash = 2166136261U;
    uint32_t index;
    for (index = 0U; index < sizeof(handle); index++) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    hash &= 0x7FFFFFFFU;
    if (hash == 0U) hash = 1U;
    return hash | token_namespace;
}

bool sm64_saturn_scene_residency_consumer_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer, uint32_t reference_token)
{
    sm64_saturn_scene_resident_identity_t *identity = find_resident(state, generation);
    int index = consumer_index(consumer);
    uint32_t slot, available = SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES;
    if (identity == NULL || index < 0 || generation != state->active_generation ||
        (identity->consumer_open_mask & consumer) == 0U ||
        reference_token == 0U) return false;
    for (slot = 0U; slot < SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES; slot++) {
        if (identity->consumer_reference_token[index][slot] == reference_token) return false;
        if (identity->consumer_reference_token[index][slot] == 0U &&
            available == SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES) available = slot;
    }
    if (available == SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES) return false;
    identity->consumer_reference_token[index][available] = reference_token;
    identity->consumer_reference_count[index]++;
    return true;
}

bool sm64_saturn_scene_residency_consumer_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer, uint32_t reference_token)
{
    sm64_saturn_scene_resident_identity_t *identity = find_resident(state, generation);
    int index = consumer_index(consumer);
    uint32_t slot;
    if (identity == NULL || index < 0 || reference_token == 0U ||
        identity->consumer_reference_count[index] == 0U) return false;
    for (slot = 0U; slot < SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES; slot++)
        if (identity->consumer_reference_token[index][slot] == reference_token) {
            identity->consumer_reference_token[index][slot] = 0U;
            identity->consumer_reference_count[index]--;
            return true;
        }
    return false;
}

static bool snapshot_matches(const sm64_saturn_scene_resident_identity_t *identity,
                             const sm64_saturn_render_snapshot_t *snapshot)
{
    return identity != NULL && snapshot != NULL && snapshot->generation == identity->generation &&
        snapshot->scene_package_id == identity->scene_package_id &&
        bytes_equal(snapshot->scene_package_sha256, identity->package_sha256) &&
        bytes_equal(snapshot->scene_dependency_set_sha256, identity->dependency_set_sha256);
}

bool sm64_saturn_scene_residency_render_snapshot_acquire(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_render_snapshot *snapshot)
{
    sm64_saturn_scene_resident_identity_t *identity =
        find_resident(state, snapshot == NULL ? 0U : snapshot->generation);
    return snapshot_matches(identity, snapshot) && sm64_saturn_scene_residency_consumer_acquire(
        state, snapshot->generation, SM64_SATURN_SCENE_RETIRE_RENDER,
        reference_token_from_handle(snapshot, 0U));
}

bool sm64_saturn_scene_residency_render_snapshot_release(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_render_snapshot *snapshot)
{
    sm64_saturn_scene_resident_identity_t *identity =
        find_resident(state, snapshot == NULL ? 0U : snapshot->generation);
    return snapshot_matches(identity, snapshot) && sm64_saturn_scene_residency_consumer_release(
        state, snapshot->generation, SM64_SATURN_SCENE_RETIRE_RENDER,
        reference_token_from_handle(snapshot, 0U));
}

bool sm64_saturn_scene_residency_vdp1_frame_bank_acquire(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_vdp1_frame_bank *bank)
{
    return bank != NULL && sm64_saturn_scene_residency_consumer_acquire(
        state, bank->snapshot_generation, SM64_SATURN_SCENE_RETIRE_BANK,
        reference_token_from_handle(bank, 0x80000000U));
}

bool sm64_saturn_scene_residency_vdp1_frame_bank_release(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_vdp1_frame_bank *bank)
{
    return bank != NULL && sm64_saturn_scene_residency_consumer_release(
        state, bank->snapshot_generation, SM64_SATURN_SCENE_RETIRE_BANK,
        reference_token_from_handle(bank, 0x80000000U));
}

bool sm64_saturn_scene_residency_actor_bank_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t bank_token)
{
    if (bank_token > 0x7FFFFFFFU) return false;
    return sm64_saturn_scene_residency_consumer_acquire(
        state, generation, SM64_SATURN_SCENE_RETIRE_BANK, bank_token);
}

bool sm64_saturn_scene_residency_actor_bank_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t bank_token)
{
    if (bank_token > 0x7FFFFFFFU) return false;
    return sm64_saturn_scene_residency_consumer_release(
        state, generation, SM64_SATURN_SCENE_RETIRE_BANK, bank_token);
}

bool sm64_saturn_scene_residency_audio_voice_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t voice_token)
{
    return sm64_saturn_scene_residency_consumer_acquire(
        state, generation, SM64_SATURN_SCENE_RETIRE_VOICE, voice_token);
}

bool sm64_saturn_scene_residency_audio_voice_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t voice_token)
{
    return sm64_saturn_scene_residency_consumer_release(
        state, generation, SM64_SATURN_SCENE_RETIRE_VOICE, voice_token);
}

bool sm64_saturn_scene_residency_unload(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation)
{
    sm64_saturn_scene_resident_identity_t *identity;
    uint32_t index;
    if (state == NULL || generation == 0U || generation == state->active_generation) return false;
    identity = find_resident(state, generation);
    if (identity == NULL || identity->consumer_open_mask != 0U) return false;
    for (index = 0U; index < 3U; index++) {
        uint32_t token;
        if (identity->consumer_reference_count[index] != 0U) return false;
        for (token = 0U; token < SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES; token++)
            if (identity->consumer_reference_token[index][token] != 0U) return false;
    }
    if (state->actor_texture_publication.generation == generation)
        actor_texture_publication_invalidate(state);
    clear_identity_storage(state, identity);
    memset(identity, 0, sizeof(*identity));
    return true;
}

const sm64_saturn_scene_resident_identity_t *
sm64_saturn_scene_residency_active(const sm64_saturn_scene_residency_t *state)
{
    uint32_t slot;
    if (state == NULL || state->active_generation == 0U) return NULL;
    for (slot = 0U; slot < 2U; slot++)
        if (state->slot[slot].committed && state->slot[slot].generation == state->active_generation)
            return &state->slot[slot];
    return NULL;
}

bool sm64_saturn_scene_residency_snapshot_apply(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    struct sm64_saturn_render_snapshot *snapshot)
{
    const sm64_saturn_scene_resident_identity_t *identity =
        sm64_saturn_scene_residency_active(state);
    if (identity == NULL || snapshot == NULL || identity->generation != generation ||
        snapshot->generation != generation) return false;
    snapshot->scene_package_id = identity->scene_package_id;
    snapshot->active_feature_mask = identity->active_feature_mask;
    memcpy(snapshot->scene_package_sha256, identity->package_sha256, 32U);
    memcpy(snapshot->scene_dependency_set_sha256, identity->dependency_set_sha256, 32U);
    memcpy(snapshot->actor_bank_identity, identity->actor_bank_identity, 32U);
    memcpy(snapshot->animation_bank_identity, identity->animation_bank_identity, 32U);
    memcpy(snapshot->audio_bank_identity, identity->audio_bank_identity, 32U);
    return true;
}

const uint8_t *sm64_saturn_scene_residency_root_bytes(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t *byte_count)
{
    uint32_t slot;
    if (byte_count != NULL) *byte_count = 0U;
    if (state == NULL || byte_count == NULL) return NULL;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed &&
        state->slot[slot].generation == generation) {
        *byte_count = state->slot[slot].root_byte_count;
        return state->root_storage + state->slot[slot].root_storage_offset;
    }
    return NULL;
}

const uint8_t *sm64_saturn_scene_residency_dependency_bytes(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint16_t dependency_index, uint32_t *byte_count)
{
    uint32_t slot;
    if (byte_count != NULL) *byte_count = 0U;
    if (state == NULL || byte_count == NULL || dependency_index >= SM64_SATURN_SCENE_MAX_DEPENDENCIES)
        return NULL;
    for (slot = 0U; slot < 2U; slot++) if (state->slot[slot].committed &&
        state->slot[slot].generation == generation) {
        const sm64_saturn_scene_dependency_view_t *dependency;
        sm64_saturn_scene_package_view_t view;
        uint32_t offset = state->slot[slot].dependency_storage_offset[dependency_index];
        if (offset == UINT32_MAX || !sm64_saturn_scene_package_validate(
                state->root_storage + state->slot[slot].root_storage_offset,
                state->slot[slot].root_byte_count, &view) || dependency_index >= view.dependency_count) return NULL;
        dependency = &view.dependencies[dependency_index];
        *byte_count = dependency->byte_count;
        return state->destination_storage[dependency->destination_class] + offset;
    }
    return NULL;
}

sm64_saturn_actor_texture_publication_t *
sm64_saturn_scene_residency_actor_texture_staging(
    sm64_saturn_scene_residency_t *state, uint32_t generation)
{
    if (state == NULL || generation == 0U ||
        state->staging_generation != generation)
        return NULL;
    if (!actor_texture_publication_empty(&state->actor_texture_publication) &&
        state->actor_texture_publication.generation != generation)
        return NULL;
    return &state->actor_texture_publication;
}

const sm64_saturn_actor_texture_publication_t *
sm64_saturn_scene_residency_actor_texture_active(
    const sm64_saturn_scene_residency_t *state, uint32_t generation)
{
    if (state == NULL || generation == 0U ||
        state->active_generation != generation ||
        state->actor_texture_publication.committed != 1U ||
        state->actor_texture_publication.generation != generation)
        return NULL;
    return &state->actor_texture_publication;
}
