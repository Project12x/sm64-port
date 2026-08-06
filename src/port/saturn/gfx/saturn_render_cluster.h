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
#include "../gpl/ztreme_hot_promotion.h"

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
    /* Optional package-local node identity. Zero is a valid first node; a
     * caller that does not publish node ownership leaves this field zero and
     * uses the generic scene-admission reference table instead. */
    uint16_t scene_node;
    uint16_t scene_flags;
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

/* Generation zero is reserved as the invalid/unpublished value throughout
 * the render handoff contracts. Keep the wrap rule at the shared admission
 * boundary so the caller derives one value for admission and publication. */
static inline uint32_t sm64_saturn_render_generation_next(uint32_t current)
{
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

/* Project an AABB's eight corners onto the immutable Q16.16 view-forward
 * axis without materializing those corners. Each axis independently selects
 * its min/max contribution, producing conservative view-space depth bounds. */
static inline void sm64_saturn_render_cluster_view_depth_bounds(
    const sm64_saturn_render_cluster_t *cluster,
    const sm64_saturn_render_view_t *view, int32_t *nearest_depth,
    int32_t *furthest_depth)
{
    int64_t nearest_q32 = 0;
    int64_t furthest_q32 = 0;
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        const int64_t minimum = (int64_t)cluster->bounds_min_q16[axis] -
                                view->camera_position_q16[axis];
        const int64_t maximum = (int64_t)cluster->bounds_max_q16[axis] -
                                view->camera_position_q16[axis];
        const int64_t forward = view->view_forward_q16[axis];
        if (forward >= 0) {
            nearest_q32 += minimum * forward;
            furthest_q32 += maximum * forward;
        } else {
            nearest_q32 += maximum * forward;
            furthest_q32 += minimum * forward;
        }
    }
    const int64_t nearest_q16 = nearest_q32 >> 16;
    const int64_t furthest_q16 = furthest_q32 >> 16;
    *nearest_depth = nearest_q16 > INT32_MAX ? INT32_MAX :
                     nearest_q16 < INT32_MIN ? INT32_MIN :
                     (int32_t)nearest_q16;
    *furthest_depth = furthest_q16 > INT32_MAX ? INT32_MAX :
                      furthest_q16 < INT32_MIN ? INT32_MIN :
                      (int32_t)furthest_q16;
}

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

    if (view->view_forward_q16[0] == 0 && view->view_forward_q16[1] == 0 &&
        view->view_forward_q16[2] == 0)
        return false;

    int32_t nearest_depth;
    int32_t furthest_depth;
    sm64_saturn_render_cluster_view_depth_bounds(
        cluster, view, &nearest_depth, &furthest_depth);
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
