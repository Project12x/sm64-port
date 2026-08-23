#include <stddef.h>

#include "saturn_hud_publish.h"

void
sm64_saturn_hud_publish(const sm64_saturn_hud_snapshot_t *snapshot)
{
    if (snapshot == NULL)
        return;

    sm64_saturn_hud_cell_t next_cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t next_count = sm64_saturn_hud_layout_build(
        snapshot, next_cells, SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
#if !defined(SM64_SATURN_HUD_TEST_MUTATE_SKIP_CLEAR)
    for (uint8_t row = 0U; row < 14U; row++) {
        for (uint8_t col = 0U; col < 20U; col++)
            sm64_saturn_hud_atlas_write_cell(col, row,
                                              SM64_SATURN_HUD_GLYPH_BLANK);
    }
#endif
    for (uint32_t index = 0U; index < next_count; index++) {
        const sm64_saturn_hud_cell_t *const next = &next_cells[index];
        sm64_saturn_hud_atlas_write_cell(next->col, next->row, next->glyph);
    }
    /* S4.2: the pause panel is drawn last so it sits over the HUD rather than
     * being erased by the clear above. Pausing previously showed nothing at
     * all -- the game stopped responding with no indication it had paused,
     * which reads as a lock-up to the player. */
    sm64_saturn_hud_draw_menu_panel(snapshot->menu_mode_plus_one);
}

/* Centred single-line banner. The alphabet has no J/Q/V/X/Z, so wording here
 * is constrained -- "PAUSE" and "COURSE COMPLETE" are drawable, "EXIT" is not.
 * write_string reports an undrawable character rather than rendering a hole,
 * and the assert-free fallback is a blank cell. */
void
sm64_saturn_hud_draw_menu_panel(uint8_t menu_mode_plus_one)
{
    static const uint8_t PANEL_ROW = 6U;
    const char *text;
    uint8_t width;
    uint8_t col;

    switch (menu_mode_plus_one) {
    case SM64_SATURN_HUD_MENU_PAUSE:    text = "PAUSE"; break;
    case SM64_SATURN_HUD_MENU_COMPLETE: text = "COURSE COMPLETE"; break;
    default: return;
    }
    for (width = 0U; text[width] != '\0'; width++) { }
    if (width > SM64_SATURN_HUD_COLS) return;
    col = (uint8_t)((SM64_SATURN_HUD_COLS - width) / 2U);
    /* One blank row above and below so the banner reads as a panel against
     * whatever geometry is frozen behind it. */
    sm64_saturn_hud_atlas_fill(0U, (uint8_t)(PANEL_ROW - 1U),
                               SM64_SATURN_HUD_COLS, 3U,
                               SM64_SATURN_HUD_GLYPH_BLANK);
    (void)sm64_saturn_hud_atlas_write_string(col, PANEL_ROW, text);
}
