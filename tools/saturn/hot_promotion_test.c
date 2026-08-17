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

    /* T2.19c anti-popping oracle.  A command-count LOD changes *what is
     * drawn* at a tier boundary, so a camera drifting across that boundary
     * must not be able to make the tier oscillate -- a static quality drop is
     * nearly invisible, a per-frame snap is not.  The enter/exit band is the
     * only thing that prevents it, so pin it directly: dither the depth
     * across each enter threshold by +/-1 unit for many frames and require
     * exactly zero tier changes after the first entry.
     *
     * `verify-hot-promotion` compiles this same source with
     * SM64_SATURN_LOD_TEST_NO_HYSTERESIS, which collapses exit onto enter;
     * this loop is what turns that mutation into a failure. */
    {
        typedef struct { int32_t depth; uint16_t span; } dither_t;
        dither_t dither[2];
        /* MID boundary: depth 4096, span comfortably inside both windows. */
        dither[0] = (dither_t){4096, 64U};
        dither[1] = (dither_t){4095, 64U};
        saturn_lod_tier_t tier = saturn_lod_select(
            SATURN_LOD_NEAR, dither[0].depth, dither[0].span, &thresholds);
        if (tier != SATURN_LOD_MID) return 30;
        for (int frame = 0; frame < 64; frame++) {
            const int phase = frame & 1;
            const saturn_lod_tier_t next = saturn_lod_select(
                tier, dither[phase].depth, dither[phase].span, &thresholds);
            if (next != tier) return 31;
            tier = next;
        }
        /* FAR boundary: depth 7168, span inside the far enter window. */
        dither[0] = (dither_t){7168, 32U};
        dither[1] = (dither_t){7167, 32U};
        tier = saturn_lod_select(SATURN_LOD_NEAR, dither[0].depth,
                                 dither[0].span, &thresholds);
        if (tier != SATURN_LOD_FAR) return 32;
        for (int frame = 0; frame < 64; frame++) {
            const int phase = frame & 1;
            const saturn_lod_tier_t next = saturn_lod_select(
                tier, dither[phase].depth, dither[phase].span, &thresholds);
            if (next != tier) return 33;
            tier = next;
        }
        /* The span axis carries the same band and the same risk. */
        tier = saturn_lod_select(SATURN_LOD_NEAR, 7168, 40U, &thresholds);
        if (tier != SATURN_LOD_FAR) return 34;
        for (int frame = 0; frame < 64; frame++) {
            const uint16_t span = (frame & 1) ? 41U : 40U;
            const saturn_lod_tier_t next =
                saturn_lod_select(tier, 7168, span, &thresholds);
            if (next != tier) return 35;
            tier = next;
        }
        /* And the band must not be so wide that the tier can never leave:
         * a genuine departure past the exit window still demotes. */
        if (saturn_lod_select(SATURN_LOD_FAR, 6143, 44U, &thresholds) ==
            SATURN_LOD_FAR) return 36;
    }

    uint8_t tiers[] = {SATURN_LOD_FAR, SATURN_LOD_MID, SATURN_LOD_NEAR};
    saturn_lod_reset(tiers, sizeof(tiers));
    if (tiers[0] != SATURN_LOD_NEAR || tiers[1] != SATURN_LOD_NEAR ||
        tiers[2] != SATURN_LOD_NEAR) return 12;

    /* Simulate Sourceboot's catch-up loop: each authoritative tick observes
     * scene state before the next tick can hide an exit/re-entry. */
    saturn_lod_scene_t scene;
    saturn_lod_scene_init(&scene);
    tiers[0] = saturn_lod_select(SATURN_LOD_NEAR, 7200, 32, &thresholds);
    if (tiers[0] != SATURN_LOD_FAR ||
        !saturn_lod_scene_observe(&scene, true, 9, 1, tiers, sizeof(tiers)) ||
        tiers[0] != SATURN_LOD_NEAR) return 13;
    tiers[0] = saturn_lod_select(SATURN_LOD_NEAR, 7200, 32, &thresholds);
    tiers[0] = saturn_lod_select((saturn_lod_tier_t)tiers[0], 6400, 49,
                                 &thresholds);
    if (tiers[0] != SATURN_LOD_MID) return 14;
    tiers[0] = saturn_lod_select((saturn_lod_tier_t)tiers[0], 3500, 113,
                                 &thresholds);
    if (tiers[0] != SATURN_LOD_NEAR) return 15;
    tiers[0] = SATURN_LOD_FAR;
    if (!saturn_lod_scene_observe(&scene, false, 9, 1, tiers, sizeof(tiers)) ||
        tiers[0] != SATURN_LOD_NEAR)
        return 16;
    tiers[0] = SATURN_LOD_FAR;
    if (!saturn_lod_scene_observe(&scene, true, 9, 1, tiers, sizeof(tiers)) ||
        tiers[0] != SATURN_LOD_NEAR) return 17;
    tiers[0] = SATURN_LOD_FAR;
    if (!saturn_lod_scene_observe(&scene, true, 10, 1, tiers, sizeof(tiers)) ||
        tiers[0] != SATURN_LOD_NEAR) return 18;
    tiers[0] = SATURN_LOD_MID;
    if (saturn_lod_scene_observe(&scene, true, 10, 1, tiers, sizeof(tiers)) ||
        tiers[0] != SATURN_LOD_MID)
        return 19;
    /* Route-prefix and bake approval are correctness invariants, not
     * performance gates: the candidate may only suppress approved optional
     * primitives outside the protected prefix. */
    if (saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, true, 128U, 128U) != true ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, true, 127U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 2U, false, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_MID, 2U, true, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 1U, true, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, 3U, true, 1024U, 128U) != false ||
        saturn_lod_can_suppress(SATURN_LOD_FAR, UINT8_MAX, true, 1024U, 128U) != false)
        return 20;
    if (!saturn_lod_can_degrade_material(SATURN_LOD_MID, 1U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_NEAR, 2U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, 1U, false, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, 0U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, 3U, true, true) ||
        saturn_lod_can_degrade_material(SATURN_LOD_MID, UINT8_MAX, true, true))
        return 21;
    puts("hot promotion contract: PASS");
    return 0;
}
