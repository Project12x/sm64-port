/* See ztreme_hot_promotion.h for the GPL source and change notice. */
#include "ztreme_hot_promotion.h"

#include <string.h>

static size_t align_up(size_t value, size_t alignment) {
    const size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

void saturn_hot_promotion_init(saturn_hot_promotion_t *promotion,
                               void *destination, size_t capacity) {
    promotion->destination = (uint8_t *)destination;
    promotion->capacity = capacity;
    promotion->used = 0U;
}

void *saturn_hot_promote(saturn_hot_promotion_t *promotion,
                         const void *source, size_t size, size_t alignment) {
    size_t offset;
    void *destination;

    if (promotion == NULL || source == NULL || size == 0U ||
        alignment == 0U || (alignment & (alignment - 1U)) != 0U)
        return NULL;
    offset = align_up(promotion->used, alignment);
    if (offset > promotion->capacity || size > promotion->capacity - offset)
        return NULL;
    destination = promotion->destination + offset;
    /* LWRAM is the cold source; only this bounded copy enters the hot arena.
     * The frame loop receives the returned HWRAM pointer and never follows
     * the source pointer after promotion. */
    memcpy(destination, source, size);
    promotion->used = offset + size;
    return destination;
}

size_t saturn_hot_promotion_used(const saturn_hot_promotion_t *promotion) {
    return promotion == NULL ? 0U : promotion->used;
}

saturn_lod_thresholds_t saturn_lod_default_thresholds(void)
{
    return (saturn_lod_thresholds_t){
        .mid_enter_depth = 4096,
        .mid_exit_depth = 3584,
        .far_enter_depth = 7168,
        .far_exit_depth = 6144,
        .mid_enter_span = 96U,
        .mid_exit_span = 112U,
        .far_enter_span = 40U,
        .far_exit_span = 48U,
    };
}

static bool lod_enters_mid(int32_t depth, uint16_t span,
                           const saturn_lod_thresholds_t *thresholds)
{
    return depth >= thresholds->mid_enter_depth &&
           span <= thresholds->mid_enter_span;
}

static bool lod_stays_mid(int32_t depth, uint16_t span,
                          const saturn_lod_thresholds_t *thresholds)
{
    return depth >= thresholds->mid_exit_depth &&
           span <= thresholds->mid_exit_span;
}

static bool lod_enters_far(int32_t depth, uint16_t span,
                           const saturn_lod_thresholds_t *thresholds)
{
    return depth >= thresholds->far_enter_depth &&
           span <= thresholds->far_enter_span;
}

static bool lod_stays_far(int32_t depth, uint16_t span,
                          const saturn_lod_thresholds_t *thresholds)
{
    return depth >= thresholds->far_exit_depth &&
           span <= thresholds->far_exit_span;
}

saturn_lod_tier_t saturn_lod_select(saturn_lod_tier_t previous,
                                    int32_t depth, uint16_t projected_span,
                                    const saturn_lod_thresholds_t *thresholds)
{
    const saturn_lod_thresholds_t defaults = saturn_lod_default_thresholds();
    if (thresholds == NULL)
        thresholds = &defaults;
    if (previous > SATURN_LOD_FAR)
        previous = SATURN_LOD_NEAR;

    if (previous == SATURN_LOD_FAR) {
        if (lod_stays_far(depth, projected_span, thresholds))
            return SATURN_LOD_FAR;
        return lod_enters_mid(depth, projected_span, thresholds)
            ? SATURN_LOD_MID : SATURN_LOD_NEAR;
    }
    if (previous == SATURN_LOD_MID) {
        if (lod_enters_far(depth, projected_span, thresholds))
            return SATURN_LOD_FAR;
        return lod_stays_mid(depth, projected_span, thresholds)
            ? SATURN_LOD_MID : SATURN_LOD_NEAR;
    }
    if (lod_enters_far(depth, projected_span, thresholds))
        return SATURN_LOD_FAR;
    return lod_enters_mid(depth, projected_span, thresholds)
        ? SATURN_LOD_MID : SATURN_LOD_NEAR;
}

void saturn_lod_reset(uint8_t *tiers, size_t count)
{
    if (tiers != NULL)
        memset(tiers, SATURN_LOD_NEAR, count);
}

bool saturn_lod_can_suppress(saturn_lod_tier_t tier, uint8_t build_role,
                             bool bake_approved_optional, uint16_t source_id,
                             uint16_t mandatory_route_prefix)
{
    return build_role >= 2U && tier == SATURN_LOD_FAR &&
           bake_approved_optional && source_id >= mandatory_route_prefix;
}

bool saturn_lod_can_degrade_material(saturn_lod_tier_t tier,
                                     uint8_t build_role, bool textured,
                                     bool expensive_material)
{
    return build_role >= 1U && tier >= SATURN_LOD_MID && textured &&
           expensive_material;
}
