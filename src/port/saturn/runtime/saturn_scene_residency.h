#ifndef SM64_SATURN_SCENE_RESIDENCY_H
#define SM64_SATURN_SCENE_RESIDENCY_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_scene_package.h"
#include "../gfx/saturn_actor_texture_residency.h"
#include "../platform/saturn_build_identity.h"

struct sm64_saturn_render_snapshot;
struct sm64_saturn_vdp1_frame_bank;

enum {
    SM64_SATURN_SCENE_FEATURE_COMPLETE_MARIO_ANIMATION = SM64_SATURN_FEATURE_COMPLETE_MARIO_ANIMATION,
    SM64_SATURN_SCENE_FEATURE_DYNAMIC_ACTOR_CLOSURE = SM64_SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE,
    SM64_SATURN_SCENE_FEATURE_SEMANTIC_AUDIO = SM64_SATURN_FEATURE_SEMANTIC_AUDIO,
    SM64_SATURN_SCENE_RETIRE_RENDER = 1U << 0,
    SM64_SATURN_SCENE_RETIRE_BANK = 1U << 1,
    SM64_SATURN_SCENE_RETIRE_VOICE = 1U << 2,
    SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES = 8U,
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
    uint32_t consumer_open_mask;
    uint16_t consumer_reference_count[3];
    uint16_t reserved_consumer;
    uint32_t consumer_reference_token[3][SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES];
    uint32_t root_storage_offset;
    uint32_t root_byte_count;
    uint32_t destination_base[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint32_t destination_bytes[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint32_t section_storage_offset[SM64_SATURN_SCENE_MAX_SECTIONS];
    uint32_t dependency_storage_offset[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
    /* Commit-time copies avoid rebuilding the 3.3 KiB package view on the
     * SH-2 stack for every dependency lookup. Cost: 160 bytes per resident
     * generation, 320 bytes for the fixed two-slot owner. */
    uint32_t dependency_byte_count[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
    uint8_t dependency_destination_class[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
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
    uint8_t *root_storage;
    uint32_t root_storage_capacity;
    uint8_t *destination_storage[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT];
    uint32_t active_feature_mask;
    uint32_t active_generation;
    uint32_t staging_generation;
    uint32_t loaded_section_mask;
    uint32_t loaded_dependency_mask;
    uint16_t payload_count;
    int8_t staging_slot;
    uint8_t quarantine_count;
    sm64_saturn_actor_texture_publication_t actor_texture_publication;
} sm64_saturn_scene_residency_t;

void sm64_saturn_scene_residency_reset(
    sm64_saturn_scene_residency_t *state, uint32_t active_feature_mask,
    const uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT]);
bool sm64_saturn_scene_residency_bind_payloads(
    sm64_saturn_scene_residency_t *state,
    const sm64_saturn_scene_payload_source_t *payloads, uint16_t payload_count);
bool sm64_saturn_scene_residency_bind_storage(
    sm64_saturn_scene_residency_t *state, void *root_storage,
    uint32_t root_storage_capacity,
    void *const destination_storage[SM64_SATURN_SCENE_DESTINATION_COUNT]);
bool sm64_saturn_scene_residency_begin(sm64_saturn_scene_residency_t *state,
                                       const sm64_saturn_scene_package_view_t *view,
                                       uint32_t generation);
bool sm64_saturn_scene_residency_load_section(sm64_saturn_scene_residency_t *state,
                                              uint16_t section_index);
bool sm64_saturn_scene_residency_commit(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
/*
 * reference_token is a nonzero lease identity. It must be unique among live
 * leases for the same consumer and remain bound to that lease until its one
 * successful release. Duplicate acquire/release and unknown tokens fail closed.
 */
bool sm64_saturn_scene_residency_consumer_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer, uint32_t reference_token);
bool sm64_saturn_scene_residency_consumer_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t consumer, uint32_t reference_token);
bool sm64_saturn_scene_residency_render_snapshot_acquire(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_render_snapshot *snapshot);
bool sm64_saturn_scene_residency_render_snapshot_release(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_render_snapshot *snapshot);
bool sm64_saturn_scene_residency_vdp1_frame_bank_acquire(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_vdp1_frame_bank *bank);
bool sm64_saturn_scene_residency_vdp1_frame_bank_release(
    sm64_saturn_scene_residency_t *state,
    const struct sm64_saturn_vdp1_frame_bank *bank);
/*
 * Actor-bank and audio-voice callers supply the lease token described above.
 * Actor-bank tokens are limited to 1..0x7fffffff; VDP1 frame handles occupy
 * the disjoint high-bit namespace of the shared bank-consumer token set.
 */
bool sm64_saturn_scene_residency_actor_bank_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t bank_token);
bool sm64_saturn_scene_residency_actor_bank_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t bank_token);
bool sm64_saturn_scene_residency_audio_voice_acquire(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t voice_token);
bool sm64_saturn_scene_residency_audio_voice_release(
    sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t voice_token);
bool sm64_saturn_scene_residency_unload(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
const sm64_saturn_scene_resident_identity_t *
sm64_saturn_scene_residency_active(const sm64_saturn_scene_residency_t *state);
bool sm64_saturn_scene_residency_snapshot_apply(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    struct sm64_saturn_render_snapshot *snapshot);
const uint8_t *sm64_saturn_scene_residency_root_bytes(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint32_t *byte_count);
const uint8_t *sm64_saturn_scene_residency_dependency_bytes(
    const sm64_saturn_scene_residency_t *state, uint32_t generation,
    uint16_t dependency_index, uint32_t *byte_count);
sm64_saturn_actor_texture_publication_t *
sm64_saturn_scene_residency_actor_texture_staging(
    sm64_saturn_scene_residency_t *state, uint32_t generation);
const sm64_saturn_actor_texture_publication_t *
sm64_saturn_scene_residency_actor_texture_active(
    const sm64_saturn_scene_residency_t *state, uint32_t generation);

#endif
