/*
 * GPL-3.0. Close-port of the hot vertex promotion pattern in
 * Maxime-XL2/SONIC-Z-TREME, cff75451, Projects/SONIC Z-TREME/ZTE/
 * ZT_LOADING.c:320-353.
 *
 * Material change: the Saturn port exposes a bounded offset-free bank API
 * instead of mutating SGL mesh globals; callers own the LWRAM source and the
 * HWRAM destination arena. See docs/saturn/PROVENANCE.md.
 */
#ifndef SM64_SATURN_ZTREME_HOT_PROMOTION_H
#define SM64_SATURN_ZTREME_HOT_PROMOTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct saturn_hot_promotion {
    uint8_t *destination;
    size_t capacity;
    size_t used;
} saturn_hot_promotion_t;

void saturn_hot_promotion_init(saturn_hot_promotion_t *promotion,
                               void *destination, size_t capacity);
void *saturn_hot_promote(saturn_hot_promotion_t *promotion,
                         const void *source, size_t size, size_t alignment);
size_t saturn_hot_promotion_used(const saturn_hot_promotion_t *promotion);

/* Original bounded policy API, informed by the per-leaf full/LOD selection
 * pattern studied in Maxime-XL2/SONIC-Z-TREME at cff75451 (GPL-3.0),
 * ZT_RENDERING.c:718-786 and ZT_LOADING.c:299-355.  It deliberately does not
 * adopt the SGL model structures: this port's generated BOB primitives retain
 * source identity, route protection and VDP1 material ownership. */
typedef enum saturn_lod_tier {
    SATURN_LOD_NEAR = 0,
    SATURN_LOD_MID = 1,
    SATURN_LOD_FAR = 2,
} saturn_lod_tier_t;

typedef struct saturn_lod_thresholds {
    int32_t mid_enter_depth;
    int32_t mid_exit_depth;
    int32_t far_enter_depth;
    int32_t far_exit_depth;
    uint16_t mid_enter_span;
    uint16_t mid_exit_span;
    uint16_t far_enter_span;
    uint16_t far_exit_span;
} saturn_lod_thresholds_t;

saturn_lod_thresholds_t saturn_lod_default_thresholds(void);
saturn_lod_tier_t saturn_lod_select(saturn_lod_tier_t previous,
                                    int32_t depth, uint16_t projected_span,
                                    const saturn_lod_thresholds_t *thresholds);
void saturn_lod_reset(uint8_t *tiers, size_t count);
bool saturn_lod_can_suppress(saturn_lod_tier_t tier, uint8_t build_role,
                             bool bake_approved_optional, uint16_t source_id,
                             uint16_t mandatory_route_prefix);
bool saturn_lod_can_degrade_material(saturn_lod_tier_t tier,
                                     uint8_t build_role, bool textured,
                                     bool expensive_material);

#endif
