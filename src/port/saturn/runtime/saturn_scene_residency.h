#ifndef SM64_SATURN_SCENE_RESIDENCY_H
#define SM64_SATURN_SCENE_RESIDENCY_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_scene_package.h"
#include "../platform/saturn_build_identity.h"

enum {
    SM64_SATURN_SCENE_FEATURE_COMPLETE_MARIO_ANIMATION = SM64_SATURN_FEATURE_COMPLETE_MARIO_ANIMATION,
    SM64_SATURN_SCENE_FEATURE_DYNAMIC_ACTOR_CLOSURE = SM64_SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE,
    SM64_SATURN_SCENE_FEATURE_SEMANTIC_AUDIO = SM64_SATURN_FEATURE_SEMANTIC_AUDIO,
    SM64_SATURN_SCENE_RETIRE_RENDER = 1U << 0,
    SM64_SATURN_SCENE_RETIRE_BANK = 1U << 1,
    SM64_SATURN_SCENE_RETIRE_VOICE = 1U << 2,
};

typedef struct sm64_saturn_scene_payload_source {
    const void *bytes;
    uint32_t byte_count;
    uint32_t generation;
    uint8_t stable_id[32];
} sm64_saturn_scene_payload_source_t;

typedef struct sm64_saturn_scene_resident_identity {
    uint32_t generation;
    uint32_t scene_package_id;
    uint32_t active_feature_mask;
    uint32_t retirement_pending;
    uint32_t destination_bytes[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint8_t package_sha256[32];
    uint8_t dependency_set_sha256[32];
    uint8_t actor_bank_identity[32];
    uint8_t animation_bank_identity[32];
    uint8_t audio_bank_identity[32];
    uint8_t committed;
    uint8_t reserved[3];
} sm64_saturn_scene_resident_identity_t;

typedef struct sm64_saturn_scene_residency {
    sm64_saturn_scene_package_view_t staging_view;
    sm64_saturn_scene_payload_source_t payloads[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
    sm64_saturn_scene_resident_identity_t slot[2];
    uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint32_t active_feature_mask;
    uint32_t active_generation;
    uint32_t staging_generation;
    uint32_t loaded_section_mask;
    uint32_t loaded_dependency_mask;
    uint16_t payload_count;
    int8_t staging_slot;
    uint8_t quarantine_count;
} sm64_saturn_scene_residency_t;

void sm64_saturn_scene_residency_reset(
    sm64_saturn_scene_residency_t *state, uint32_t active_feature_mask,
    const uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT]);
bool sm64_saturn_scene_residency_bind_payloads(
    sm64_saturn_scene_residency_t *state,
    const sm64_saturn_scene_payload_source_t *payloads, uint16_t payload_count);
bool sm64_saturn_scene_residency_begin(sm64_saturn_scene_residency_t *state,
                                       const sm64_saturn_scene_package_view_t *view,
                                       uint32_t generation);
bool sm64_saturn_scene_residency_load_section(sm64_saturn_scene_residency_t *state,
                                              uint16_t section_index);
bool sm64_saturn_scene_residency_commit(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
bool sm64_saturn_scene_residency_mark_retired(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer_mask);
bool sm64_saturn_scene_residency_unload(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
const sm64_saturn_scene_resident_identity_t *
sm64_saturn_scene_residency_active(const sm64_saturn_scene_residency_t *state);
struct sm64_saturn_render_snapshot;
bool sm64_saturn_scene_residency_snapshot_apply(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    struct sm64_saturn_render_snapshot *snapshot);

#endif
