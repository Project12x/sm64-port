#include "saturn_lod_lifetime.h"
#include "port/saturn/platform/saturn_cart_code.h"

#include <string.h>

static bool apply_scene(sm64_saturn_lod_lifetime_t *lifetime,
                        bool active, int16_t level, int16_t area)
{
    const bool changed = saturn_lod_scene_observe(
        &lifetime->scene, active, level, area,
        lifetime->tiers, lifetime->tier_count);
    if (changed && lifetime->cluster_lod != NULL)
        memset(lifetime->cluster_lod, 0, lifetime->cluster_lod_bytes);
    return changed;
}

SM64_SATURN_CART_COLD
void sm64_saturn_lod_lifetime_init(
    sm64_saturn_lod_lifetime_t *lifetime,
    uint8_t *tiers, size_t tier_count,
    void *cluster_lod, size_t cluster_lod_bytes)
{
    if (lifetime == NULL) return;
    *lifetime = (sm64_saturn_lod_lifetime_t){0};
    lifetime->tiers = tiers;
    lifetime->tier_count = tier_count;
    lifetime->cluster_lod = cluster_lod;
    lifetime->cluster_lod_bytes = cluster_lod_bytes;
    saturn_lod_scene_init(&lifetime->scene);
    if (tiers != NULL) saturn_lod_reset(tiers, tier_count);
    if (cluster_lod != NULL) memset(cluster_lod, 0, cluster_lod_bytes);
}

bool sm64_saturn_lod_lifetime_begin(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation)
{
    if (lifetime == NULL || lifetime->tiers == NULL ||
        lifetime->tier_count == 0U || generation == 0U || lifetime->active)
        return false;
    lifetime->active_generation = generation;
    lifetime->active = true;
    return true;
}

bool sm64_saturn_lod_lifetime_observe_scene(
    sm64_saturn_lod_lifetime_t *lifetime, bool active,
    int16_t level, int16_t area)
{
    if (lifetime == NULL) return false;
#if !defined(SM64_SATURN_LOD_LIFETIME_TEST_APPLY_DURING_ACTIVE)
    if (lifetime->active) {
        if (!lifetime->pending_scene_valid) {
            lifetime->pending_scene = lifetime->scene;
            lifetime->pending_scene_valid = true;
        }
        const bool changed = saturn_lod_scene_observe(
            &lifetime->pending_scene, active, level, area, NULL, 0U);
        lifetime->pending_scene_changed =
            lifetime->pending_scene_changed || changed;
        return false;
    }
#endif
    return apply_scene(lifetime, active, level, area);
}

bool sm64_saturn_lod_lifetime_select(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation,
    uint16_t primitive_index, int32_t depth, uint16_t projected_span,
    const saturn_lod_thresholds_t *thresholds,
    uint8_t *tier_out, uint8_t *transition_out)
{
#if defined(SM64_SATURN_LOD_LIFETIME_TEST_IGNORE_GENERATION)
    generation = lifetime != NULL ? lifetime->active_generation : generation;
#endif
    if (lifetime == NULL || !lifetime->active ||
        generation != lifetime->active_generation ||
        primitive_index >= lifetime->tier_count || tier_out == NULL)
        return false;
    const uint8_t previous = lifetime->tiers[primitive_index];
    const uint8_t next = thresholds != NULL
        ? (uint8_t)saturn_lod_select((saturn_lod_tier_t)previous, depth,
                                    projected_span, thresholds)
        : (uint8_t)SATURN_LOD_NEAR;
    lifetime->tiers[primitive_index] = next;
    *tier_out = next;
    if (transition_out != NULL)
        *transition_out = previous != next ? 1U : 0U;
    return true;
}

bool sm64_saturn_lod_lifetime_finish(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation)
{
    if (lifetime == NULL || !lifetime->active ||
        generation != lifetime->active_generation)
        return false;
    lifetime->active = false;
    lifetime->active_generation = 0U;
    if (lifetime->pending_scene_valid) {
        lifetime->scene = lifetime->pending_scene;
        if (lifetime->pending_scene_changed) {
            saturn_lod_reset(lifetime->tiers, lifetime->tier_count);
            if (lifetime->cluster_lod != NULL)
                memset(lifetime->cluster_lod, 0,
                       lifetime->cluster_lod_bytes);
        }
    }
    lifetime->pending_scene_valid = false;
    lifetime->pending_scene_changed = false;
    return true;
}
