/* Saturn-shaped act (star) select.
 *
 * Retail SM64 picks an act on a dedicated screen that renders 3D star models
 * through Goddard and draws its labels with the dialog system
 * (src/menu/star_select.c). None of that is reachable here: sourceboot boots
 * straight into the level via level_bob_entry and never runs the castle ->
 * course-entry flow, so src/menu/*.c compiles but is dead-stripped -- `nm` on
 * the shipped ELF finds zero act_selector symbols.
 *
 * Recreating that screen would mean bringing up Goddard and the dialog
 * renderer to choose a number between one and six. This draws the same choice
 * on the VDP2 HUD tilemap instead, which is what the hardware is good at and
 * costs nothing that is not already initialised.
 *
 * WHY IT RUNS AT BOOT, BEFORE THE LEVEL: act-gated objects are placed during
 * level load. level_cmd_place_object (src/engine/level_script.c:470) computes
 * the act mask as `1 << (gCurrActNum - 1)`, so the act must already be chosen
 * by the time the level script runs -- choosing later would spawn the wrong
 * population. The boot path has everything needed by main.c:2038:
 * smpc_peripheral_init (:1949), vdp2_tvmd_display_set (:1964), the VBlank
 * handler (:1969) and the glyph atlas.
 *
 * WORDING: the US HUD font has no J, Q, V, X or Z (see saturn_hud_atlas.h), so
 * act two cannot be "KOOPA THE QUICK" and act six cannot be "BEHIND CHAIN
 * CHOMP'S GATE" in full. The labels below are shortened to the drawable
 * alphabet rather than rendered with holes; sm64_saturn_hud_atlas_write_string
 * reports an undrawable character, and the assertion in this file's selftest
 * is what keeps a future edit from silently reintroducing one.
 */

#include "source_star_select.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <yaul.h>

#include "../gfx/saturn_hud_atlas.h"

#define STAR_SELECT_ACT_COUNT 6U
#define STAR_SELECT_FIRST_ROW 4U
#define STAR_SELECT_TITLE_ROW 1U
#define STAR_SELECT_PROMPT_ROW 12U
/* Bounded so a disconnected or stuck pad cannot hang the boot before the
 * level ever loads. Roughly ten seconds at 60Hz, after which act 1 is taken --
 * the same default the retail star select offers a fresh save file. */
#define STAR_SELECT_TIMEOUT_FRAMES 600U

static const char *const star_select_labels[STAR_SELECT_ACT_COUNT] = {
    "1 BIG BOB OMB",
    "2 FOOTRACE KOOPA",
    "3 ISLAND IN SKY",
    "4 8 RED COINS",
    "5 MARIO WINGS",
    "6 CHAIN CHOMP",
};

static uint16_t star_select_read_raw(void)
{
    smpc_peripheral_digital_t digital;
    (void)memset(&digital, 0, sizeof(digital));
    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    return (uint16_t)digital.pressed.raw;
}

static void star_select_draw(uint8_t cursor)
{
    sm64_saturn_hud_atlas_fill(0U, 0U, SM64_SATURN_HUD_COLS,
                               SM64_SATURN_HUD_ROWS,
                               SM64_SATURN_HUD_GLYPH_BLANK);
    (void)sm64_saturn_hud_atlas_write_string(3U, STAR_SELECT_TITLE_ROW,
                                             "SELECT A STAR");
    for (uint8_t act = 0U; act < STAR_SELECT_ACT_COUNT; act++) {
        const uint8_t row = (uint8_t)(STAR_SELECT_FIRST_ROW + act);
        /* The star glyph is the cursor: it already means "this act" to the
         * player from the HUD, and it costs no new artwork. */
        sm64_saturn_hud_atlas_write_cell(
            1U, row,
            act == cursor ? SM64_SATURN_HUD_GLYPH_STAR
                          : SM64_SATURN_HUD_GLYPH_BLANK);
        (void)sm64_saturn_hud_atlas_write_string(3U, row,
                                                 star_select_labels[act]);
    }
    (void)sm64_saturn_hud_atlas_write_string(2U, STAR_SELECT_PROMPT_ROW,
                                             "START TO BEGIN");
}

int sm64_saturn_star_select_run(const volatile uint32_t *vblank_counter)
{
    uint8_t cursor = 0U;
    uint16_t previous = 0U;
    bool dirty = true;

    /* NBG0 carries the glyph plane, and at this point in boot nothing has
     * switched it on: sourceboot_vdp2_layers_set() is wired as the VDP2
     * frame backend's .layers_set callback (main.c:1255) and is therefore
     * first called from inside the game loop, which has not started. The
     * atlas had uploaded its patterns and configured the plane, so cells
     * were being written correctly to a layer that simply was not being
     * displayed. Enable it for the menu, and hand it back dark afterwards so
     * the game loop's own layer setup is what turns it on for gameplay. */
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7U);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG0);

    for (uint32_t frame = 0U; frame < STAR_SELECT_TIMEOUT_FRAMES; frame++) {
        const uint16_t raw = star_select_read_raw();
        /* Edge-triggered: a held direction must not sweep the cursor, and a
         * START still down from the BIOS handoff must not select instantly. */
        const uint16_t pressed = (uint16_t)(raw & ~previous);
        previous = raw;

        if (pressed & PERIPHERAL_DIGITAL_UP)
            cursor = (uint8_t)((cursor + STAR_SELECT_ACT_COUNT - 1U) %
                               STAR_SELECT_ACT_COUNT),
            dirty = true;
        if (pressed & PERIPHERAL_DIGITAL_DOWN)
            cursor = (uint8_t)((cursor + 1U) % STAR_SELECT_ACT_COUNT),
            dirty = true;
        if (pressed & (PERIPHERAL_DIGITAL_START | PERIPHERAL_DIGITAL_A |
                       PERIPHERAL_DIGITAL_B | PERIPHERAL_DIGITAL_C))
            break;

        if (dirty) {
            star_select_draw(cursor);
            dirty = false;
        }
        /* Commit the tilemap writes, then wait one field on the caller's
         * VBlank-out counter. Without a wait this loop would burn its whole
         * frame budget in microseconds and fall through to the act-1 default
         * before anything appeared on screen. */
        vdp2_sync();
        if (vblank_counter != NULL) {
            const uint32_t mark = *vblank_counter;
            while (*vblank_counter == mark) { }
        }
    }

    sm64_saturn_hud_atlas_fill(0U, 0U, SM64_SATURN_HUD_COLS,
                               SM64_SATURN_HUD_ROWS,
                               SM64_SATURN_HUD_GLYPH_BLANK);
    vdp2_sync();
    vdp2_scrn_display_set(VDP2_SCRN_DISPTP_NONE);
    return (int)cursor + 1;
}

bool sm64_saturn_star_select_labels_are_drawable(void)
{
    for (uint8_t act = 0U; act < STAR_SELECT_ACT_COUNT; act++) {
        const char *const label = star_select_labels[act];
        for (uint8_t index = 0U; label[index] != '\0'; index++) {
            if (sm64_saturn_hud_glyph_for_char(label[index]) ==
                SM64_SATURN_HUD_GLYPH_COUNT)
                return false;
            if (index >= SM64_SATURN_HUD_COLS - 3U) return false;
        }
    }
    return true;
}
