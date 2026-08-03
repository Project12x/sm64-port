#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ztreme_hot_promotion.h"

int main(void)
{
    uint8_t source[32];
    uint8_t destination[32];
    saturn_hot_promotion_t promotion;
    for (uint8_t i = 0; i < sizeof(source); i++) source[i] = i;
    memset(destination, 0xA5, sizeof(destination));
    saturn_hot_promotion_init(&promotion, destination, sizeof(destination));

    uint8_t *first = saturn_hot_promote(&promotion, source, 7U, 8U);
    if (first != destination || promotion.used != 7U ||
        memcmp(first, source, 7U) != 0) return 1;
    uint8_t *second = saturn_hot_promote(&promotion, source + 7U, 9U, 16U);
    if (second != destination + 16U || promotion.used != 25U ||
        memcmp(second, source + 7U, 9U) != 0) return 2;
    if (saturn_hot_promote(&promotion, source, 8U, 0U) != NULL ||
        saturn_hot_promote(&promotion, source, 16U, 3U) != NULL ||
        saturn_hot_promote(&promotion, source, 16U, 16U) != NULL)
        return 3;
    if (saturn_hot_promotion_used(&promotion) != 25U) return 4;

    const saturn_lod_thresholds_t thresholds = saturn_lod_default_thresholds();
    if (saturn_lod_select(SATURN_LOD_NEAR, 4096, 64, &thresholds) !=
        SATURN_LOD_MID) return 5;
    /* A primitive remains in its current tier while the camera jitters
     * inside the wider exit window. */
    if (saturn_lod_select(SATURN_LOD_MID, 3800, 100, &thresholds) !=
        SATURN_LOD_MID) return 6;
    if (saturn_lod_select(SATURN_LOD_MID, 4096, 113, &thresholds) !=
        SATURN_LOD_NEAR) return 7;
    /* Depth alone is insufficient: large projected geometry stays NEAR. */
    if (saturn_lod_select(SATURN_LOD_NEAR, 8000, 97, &thresholds) !=
        SATURN_LOD_NEAR) return 8;
    if (saturn_lod_select(SATURN_LOD_NEAR, 7200, 32, &thresholds) !=
        SATURN_LOD_FAR) return 9;
    if (saturn_lod_select(SATURN_LOD_FAR, 6400, 44, &thresholds) !=
        SATURN_LOD_FAR) return 10;
    if (saturn_lod_select(SATURN_LOD_FAR, 6400, 49, &thresholds) !=
        SATURN_LOD_MID) return 11;

    uint8_t tiers[] = {SATURN_LOD_FAR, SATURN_LOD_MID, SATURN_LOD_NEAR};
    saturn_lod_reset(tiers, sizeof(tiers));
    if (tiers[0] != SATURN_LOD_NEAR || tiers[1] != SATURN_LOD_NEAR ||
        tiers[2] != SATURN_LOD_NEAR) return 12;
    /* Route-prefix and bake approval are correctness invariants, not
     * performance gates: the candidate may only suppress approved optional
     * primitives outside the protected prefix. */
    if (saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, true, 128U, 128U) != true ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, true, 127U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, false, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_MID, 2U, true, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 1U, true, 1024U, 128U) != false)
        return 13;
    if (!saturn_lod_can_degrade_material(SATURN_LOD_MID, 1U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_NEAR, 2U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, 1U, false, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, 0U, true, true))
        return 14;
    puts("hot promotion contract: PASS");
    return 0;
}
