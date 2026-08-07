/* tools/saturn/saturn_hud_layout_test.c */
#include <string.h>
#include <stdio.h>

#include "saturn_hud.h"
#include "saturn_hud_atlas.h"
#include "saturn_hud_layout.h"

static int
test_layout_places_lives_digit_and_glyphs(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.flags = 0x0001; /* HUD_DISPLAY_FLAG_LIVES */

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    int found_head = 0, found_x = 0, found_digit = 0;
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_MARIO_HEAD) found_head = 1;
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_MULTIPLY) found_x = 1;
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_DIGIT_4) found_digit = 1;
    }
    if (!found_head || !found_x || !found_digit) {
        fprintf(stderr, "lives readout missing head/x/digit cells\n");
        return 1;
    }
    return 0;
}

static int
test_layout_omits_lives_when_flag_clear(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.flags = 0x0000;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_DIGIT_4) {
            fprintf(stderr, "lives digit rendered with HUD_DISPLAY_NONE\n");
            return 1;
        }
    }
    return 0;
}

static int
test_layout_never_exceeds_capacity(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x004F;
    snapshot.lives = 9999;
    snapshot.coins = 9999;
    snapshot.stars = 9999;
    snapshot.timer = 0xFFFFU;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    if (count > SM64_SATURN_HUD_LAYOUT_MAX_CELLS) {
        fprintf(stderr, "layout wrote past the fixed cell buffer\n");
        return 1;
    }
    return 0;
}

static int
test_layout_reads_power_meter_from_snapshot_not_recomputed(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x0008; /* HUD_DISPLAY_FLAG_CAMERA_AND_POWER */
    snapshot.wedges = 3;
    /* Layout gates the power meter on power_meter_animation != 0, i.e.
     * != POWER_METER_HIDDEN (src/game/hud.h's enum PowerMeterAnimation:
     * HIDDEN=0, EMPHASIZED=1, DEEMPHASIZING=2, HIDING=3, VISIBLE=4). That
     * gate exactly matches hud.c's real render_hud_power_meter(), which
     * returns early only when animation == POWER_METER_HIDDEN and renders
     * for all four other phases -- so any non-zero value here is a valid
     * "meter should be showing" state, not just POWER_METER_VISIBLE.
     * memset already zeroed power_meter_animation to POWER_METER_HIDDEN,
     * which would correctly hide the meter; this test wants it visible, so
     * it must set a non-hidden phase explicitly. POWER_METER_EMPHASIZED (1)
     * is used here, matching the same numeric-literal convention Task 3's
     * saturn_hud_snapshot_test.c already established for this field (that
     * file also can't include src/game/hud.h -- it would pull in the full
     * N64 PR/ultratypes.h dependency chain into a host-only test binary). */
    snapshot.power_meter_animation = 1; /* POWER_METER_EMPHASIZED */

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    int found = 0;
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_POWER_METER_3) found = 1;
    }
    if (!found) {
        fprintf(stderr, "power meter wedge count not reflected in layout\n");
        return 1;
    }
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_layout_places_lives_digit_and_glyphs();
    failures += test_layout_omits_lives_when_flag_clear();
    failures += test_layout_never_exceeds_capacity();
    failures += test_layout_reads_power_meter_from_snapshot_not_recomputed();
    return failures;
}
