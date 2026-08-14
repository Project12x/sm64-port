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
}
