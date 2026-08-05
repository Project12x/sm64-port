#ifndef SM64_SATURN_LOD_LIFETIME_H
#define SM64_SATURN_LOD_LIFETIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../gpl/ztreme_hot_promotion.h"

/* Renderer-owned generation gate for worker-visible LOD hysteresis. A source
 * scene transition observed while work is active updates only the private
 * pending scene record; tier and cluster buffers reset after retirement. */
typedef struct sm64_saturn_lod_lifetime {
    saturn_lod_scene_t scene;
    saturn_lod_scene_t pending_scene;
    uint8_t *tiers;
    void *cluster_lod;
    size_t tier_count;
    size_t cluster_lod_bytes;
    uint32_t active_generation;
    bool active;
    bool pending_scene_valid;
    bool pending_scene_changed;
} sm64_saturn_lod_lifetime_t;

void sm64_saturn_lod_lifetime_init(
    sm64_saturn_lod_lifetime_t *lifetime,
    uint8_t *tiers, size_t tier_count,
    void *cluster_lod, size_t cluster_lod_bytes);
bool sm64_saturn_lod_lifetime_begin(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation);
bool sm64_saturn_lod_lifetime_observe_scene(
    sm64_saturn_lod_lifetime_t *lifetime, bool active,
    int16_t level, int16_t area);
bool sm64_saturn_lod_lifetime_select(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation,
    uint16_t primitive_index, int32_t depth, uint16_t projected_span,
    const saturn_lod_thresholds_t *thresholds,
    uint8_t *tier_out, uint8_t *transition_out);
bool sm64_saturn_lod_lifetime_finish(
    sm64_saturn_lod_lifetime_t *lifetime, uint32_t generation);

#endif
