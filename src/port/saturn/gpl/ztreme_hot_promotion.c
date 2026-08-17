/* See ztreme_hot_promotion.h for the GPL source and change notice. */
#include "ztreme_hot_promotion.h"
#include "port/saturn/platform/saturn_cart_code.h"

#include <string.h>

static size_t align_up(size_t value, size_t alignment) {
    const size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

SM64_SATURN_CART_COLD
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

/* Host-test mutation hooks (T2.19c).  These are never defined by any target
 * or host build recipe except the deliberately-broken compilations in
 * `verify-hot-promotion`, whose contract is that the nominal test must FAIL
 * under each one.  They exist because the tier boundaries and the
 * enter/exit hysteresis band are the two things a command-count LOD makes
 * owner-visible: a shifted boundary changes what is drawn, and a collapsed
 * band makes distant terrain snap as the camera drifts across a threshold.
 * Neither defect is caught by a test that only checks nominal values. */
#if defined(SM64_SATURN_LOD_TEST_SHIFT_FAR_BOUNDARY)
#define SATURN_LOD_FAR_ENTER_DEPTH (7168 + 1024)
#else
#define SATURN_LOD_FAR_ENTER_DEPTH 7168
#endif

saturn_lod_thresholds_t saturn_lod_default_thresholds(void)
{
    return (saturn_lod_thresholds_t){
        .mid_enter_depth = 4096,
#if defined(SM64_SATURN_LOD_TEST_NO_HYSTERESIS)
        /* Exit band collapsed onto the enter band: a camera dithering across
         * a boundary now oscillates tier every frame. */
        .mid_exit_depth = 4096,
#else
        .mid_exit_depth = 3584,
#endif
        .far_enter_depth = SATURN_LOD_FAR_ENTER_DEPTH,
#if defined(SM64_SATURN_LOD_TEST_NO_HYSTERESIS)
        .far_exit_depth = SATURN_LOD_FAR_ENTER_DEPTH,
#else
        .far_exit_depth = 6144,
#endif
        .mid_enter_span = 96U,
#if defined(SM64_SATURN_LOD_TEST_NO_HYSTERESIS)
        .mid_exit_span = 96U,
#else
        .mid_exit_span = 112U,
#endif
        .far_enter_span = 40U,
#if defined(SM64_SATURN_LOD_TEST_NO_HYSTERESIS)
        .far_exit_span = 40U,
#else
        .far_exit_span = 48U,
#endif
    };
}

/* The distance comparison itself.  Inverting it makes near geometry take the
 * cheap tier and distant geometry the expensive one -- the "LOD applied in
 * the wrong direction" defect, which costs frames *and* fidelity at once. */
#if defined(SM64_SATURN_LOD_TEST_INVERT_DEPTH_COMPARE)
#define SATURN_LOD_DEPTH_AT_LEAST(depth, limit) ((depth) <= (limit))
#else
#define SATURN_LOD_DEPTH_AT_LEAST(depth, limit) ((depth) >= (limit))
#endif

static bool lod_enters_mid(int32_t depth, uint16_t span,
                           const saturn_lod_thresholds_t *thresholds)
{
    return SATURN_LOD_DEPTH_AT_LEAST(depth, thresholds->mid_enter_depth) &&
           span <= thresholds->mid_enter_span;
}

static bool lod_stays_mid(int32_t depth, uint16_t span,
                          const saturn_lod_thresholds_t *thresholds)
{
    return SATURN_LOD_DEPTH_AT_LEAST(depth, thresholds->mid_exit_depth) &&
           span <= thresholds->mid_exit_span;
}

static bool lod_enters_far(int32_t depth, uint16_t span,
                           const saturn_lod_thresholds_t *thresholds)
{
    return SATURN_LOD_DEPTH_AT_LEAST(depth, thresholds->far_enter_depth) &&
           span <= thresholds->far_enter_span;
}

static bool lod_stays_far(int32_t depth, uint16_t span,
                          const saturn_lod_thresholds_t *thresholds)
{
    return SATURN_LOD_DEPTH_AT_LEAST(depth, thresholds->far_exit_depth) &&
           span <= thresholds->far_exit_span;
}

/* The "cheap tier applied to the wrong end of the range" mutation: the
 * closest analogue this port has to SlaveDriver halving its tile grid toward
 * the camera instead of away from it (WALLS.C:1001-1010). */
#if defined(SM64_SATURN_LOD_TEST_INVERT_TIER_ORDER)
#define SATURN_LOD_TIER(tier) \
    ((tier) == SATURN_LOD_NEAR ? SATURN_LOD_FAR \
     : (tier) == SATURN_LOD_FAR ? SATURN_LOD_NEAR : (tier))
#else
#define SATURN_LOD_TIER(tier) (tier)
#endif

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
            return SATURN_LOD_TIER(SATURN_LOD_FAR);
        return lod_enters_mid(depth, projected_span, thresholds)
            ? SATURN_LOD_MID : SATURN_LOD_TIER(SATURN_LOD_NEAR);
    }
    if (previous == SATURN_LOD_MID) {
        if (lod_enters_far(depth, projected_span, thresholds))
            return SATURN_LOD_TIER(SATURN_LOD_FAR);
        return lod_stays_mid(depth, projected_span, thresholds)
            ? SATURN_LOD_MID : SATURN_LOD_TIER(SATURN_LOD_NEAR);
    }
    if (lod_enters_far(depth, projected_span, thresholds))
        return SATURN_LOD_TIER(SATURN_LOD_FAR);
    return lod_enters_mid(depth, projected_span, thresholds)
        ? SATURN_LOD_MID : SATURN_LOD_TIER(SATURN_LOD_NEAR);
}

void saturn_lod_reset(uint8_t *tiers, size_t count)
{
    if (tiers != NULL)
        memset(tiers, SATURN_LOD_NEAR, count);
}

void saturn_lod_scene_init(saturn_lod_scene_t *scene)
{
    if (scene != NULL)
        *scene = (saturn_lod_scene_t){0, 0, 0U};
}

bool saturn_lod_scene_observe(saturn_lod_scene_t *scene, bool active,
                              int16_t level, int16_t area, uint8_t *tiers,
                              size_t tier_count)
{
    if (scene == NULL)
        return false;
    const bool changed = !active ? scene->active != 0U :
        scene->active == 0U || scene->level != level || scene->area != area;
    if (changed)
        saturn_lod_reset(tiers, tier_count);
    scene->active = active ? 1U : 0U;
    if (active) {
        scene->level = level;
        scene->area = area;
    }
    return changed;
}

bool saturn_lod_can_suppress(saturn_lod_tier_t tier, uint8_t build_role,
                             bool bake_approved_optional, uint16_t source_id,
                             uint16_t mandatory_route_prefix)
{
    return build_role == 2U && tier == SATURN_LOD_FAR &&
           bake_approved_optional && source_id >= mandatory_route_prefix;
}

bool saturn_lod_can_degrade_material(saturn_lod_tier_t tier,
                                     uint8_t build_role, bool textured,
                                     bool expensive_material)
{
    return (build_role == 1U || build_role == 2U) &&
           tier >= SATURN_LOD_MID && textured &&
           expensive_material;
}
