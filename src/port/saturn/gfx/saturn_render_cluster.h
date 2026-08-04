/* Scene-neutral early admission for fixed, generated render clusters.
 *
 * The LOD selection policy is adapted from the GPL-3.0 Sonic Z-Treme
 * cff75451c1616aac1236fc2b44223902b55c706b pattern in
 * Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c:439-480. This small generic
 * record and AABB admission code is original project code: it does not use
 * Z-Treme scene, SGL, or renderer data structures. */
#ifndef SM64_SATURN_RENDER_CLUSTER_H
#define SM64_SATURN_RENDER_CLUSTER_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_render_snapshot.h"
#include "ztreme_hot_promotion.h"

typedef struct sm64_saturn_render_cluster {
    int32_t bounds_min_q16[3];
    int32_t bounds_max_q16[3];
    uint16_t primitive_first;
    uint16_t primitive_count;
    uint16_t position_ref_first[3];
    uint16_t position_ref_count[3];
    uint16_t material_partition;
    uint16_t source_ordinal;
    uint8_t mandatory;
    uint8_t reserved[3];
} sm64_saturn_render_cluster_t;

typedef struct sm64_saturn_render_cluster_result {
    uint32_t generation;
    uint16_t primitive_first;
    uint16_t primitive_count;
    uint16_t position_ref_first;
    uint16_t position_ref_count;
    uint8_t lod_tier;
    uint8_t admitted;
} sm64_saturn_render_cluster_result_t;

typedef struct sm64_saturn_render_lod_state {
    saturn_lod_thresholds_t thresholds;
    saturn_lod_tier_t previous;
} sm64_saturn_render_lod_state_t;

static inline bool sm64_saturn_render_cluster_admit(
    const sm64_saturn_render_cluster_t *cluster,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_render_lod_state_t *lod_state,
    sm64_saturn_render_cluster_result_t *result)
{
    if (result != NULL) *result = (sm64_saturn_render_cluster_result_t){0};
    if (cluster == NULL || view == NULL || lod_state == NULL || result == NULL ||
        cluster->primitive_count == 0U)
        return false;

    const int32_t nearest_depth = cluster->bounds_min_q16[2] -
                                  view->camera_position_q16[2];
    const int32_t furthest_depth = cluster->bounds_max_q16[2] -
                                   view->camera_position_q16[2];
    if (cluster->mandatory == 0U && furthest_depth <= 0)
        return false;

    /* Bounds and view positions are Q16.16, while the shared LOD policy is
     * expressed in whole world units. Keep the conversion at this generic
     * boundary so callers cannot accidentally compare Q16 depth against raw
     * threshold constants. */
    const int32_t depth = nearest_depth > 0 ? nearest_depth >> 16 : 0;
    const saturn_lod_tier_t tier = saturn_lod_select(
        lod_state->previous, depth, 0U, &lod_state->thresholds);
    if (cluster->position_ref_count[tier] == 0U)
        return false;

    lod_state->previous = tier;
    *result = (sm64_saturn_render_cluster_result_t){
        .generation = view->generation,
        .primitive_first = cluster->primitive_first,
        .primitive_count = cluster->primitive_count,
        .position_ref_first = cluster->position_ref_first[tier],
        .position_ref_count = cluster->position_ref_count[tier],
        .lod_tier = (uint8_t)tier,
        .admitted = 1U,
    };
    return true;
}

#endif
