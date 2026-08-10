#ifndef SM64_SATURN_BUILD_IDENTITY_H
#define SM64_SATURN_BUILD_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#define SM64_SATURN_BUILD_IDENTITY_MAGIC 0x53424931U
#define SM64_SATURN_BUILD_IDENTITY_VERSION 2U
#define SM64_SATURN_BUILD_IDENTITY_SIZE 500U

enum sm64_saturn_feature_bits {
    SM64_SATURN_FEATURE_COMPLETE_MARIO_ANIMATION = 1U << 0,
    SM64_SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE = 1U << 1,
    SM64_SATURN_FEATURE_SEMANTIC_AUDIO = 1U << 2,
};

typedef struct sm64_saturn_build_identity {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t feature_bits;
    uint16_t renderer_pipeline, level_id, area_id, route_id;
    uint16_t route_replay_mode, live_input_mode;
    uint16_t camera_route, camera_variant, diagnostic_mode, reserved0;
    uint32_t bootstrap_ticks;
    uint16_t cart_mbit, cart_stage_sectors, hot_promotion, near_clip;
    uint16_t bsp_order, polygon_tier, fragment_mode, reserved1;
    uint8_t source_hash[32], effective_config_hash[32];
    uint8_t route_artifact_hash[32], input_artifact_hash[32];
    uint8_t camera_artifact_hash[32], cart_profile_hash[32];
    uint8_t scene_package_hash[32], scene_dependency_set_hash[32];
    uint8_t actor_package_hash[32], animation_package_hash[32];
    uint8_t audio_package_hash[32];
    uint8_t target_profile_hash[32];
    uint8_t package_set_root_hash[32];
    uint8_t toolchain_attestation_hash[32];
} sm64_saturn_build_identity_t;

extern const sm64_saturn_build_identity_t saturn_build_identity;
bool sm64_saturn_build_identity_is_valid(
    const sm64_saturn_build_identity_t *identity);

#endif
