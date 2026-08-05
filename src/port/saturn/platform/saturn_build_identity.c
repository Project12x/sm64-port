#include "saturn_build_identity.h"

#include "saturn_build_identity_values.inc"

_Static_assert(sizeof(sm64_saturn_build_identity_t) ==
                   SM64_SATURN_BUILD_IDENTITY_SIZE,
               "Saturn build identity ABI drift");

const sm64_saturn_build_identity_t saturn_build_identity
    __attribute__((used, section(".rodata.saturn_build_identity"))) =
        SATURN_BUILD_IDENTITY_INITIALIZER;

bool sm64_saturn_build_identity_is_valid(
    const sm64_saturn_build_identity_t *identity)
{
    const uint32_t known_features =
        SM64_SATURN_FEATURE_COMPLETE_MARIO_ANIMATION |
        SM64_SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE |
        SM64_SATURN_FEATURE_SEMANTIC_AUDIO;

    if (identity == 0 ||
        identity->magic != SM64_SATURN_BUILD_IDENTITY_MAGIC ||
        identity->version != SM64_SATURN_BUILD_IDENTITY_VERSION ||
        identity->size != SM64_SATURN_BUILD_IDENTITY_SIZE ||
        (identity->feature_bits & ~known_features) != 0U ||
        identity->reserved0 != 0U || identity->reserved1 != 0U)
        return false;
    if (identity->renderer_pipeline < 2U ||
        identity->renderer_pipeline > 4U ||
        identity->route_replay_mode > 1U || identity->live_input_mode > 1U ||
        identity->camera_route > 1U ||
        identity->camera_variant < 1U || identity->camera_variant > 3U ||
        identity->diagnostic_mode > 2U ||
        (identity->bootstrap_ticks != 0U &&
         identity->bootstrap_ticks != 600U &&
         identity->bootstrap_ticks != 1200U &&
         identity->bootstrap_ticks != 2000U) ||
        (identity->cart_mbit != 32U && identity->cart_mbit != 64U) ||
        (identity->cart_stage_sectors != 4U &&
         identity->cart_stage_sectors != 8U &&
         identity->cart_stage_sectors != 16U) ||
        identity->hot_promotion > 1U || identity->near_clip > 1U ||
        identity->bsp_order > 1U || identity->polygon_tier > 2U ||
        identity->fragment_mode > 1U)
        return false;
    return true;
}
