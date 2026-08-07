/* src/port/saturn/gfx/saturn_hud_publish.c
 *
 * Dirty-cell diff/publish layer. The staged-then-flush-only-what's-dirty
 * shape here is a pattern-only study of
 * Lobotomy-Software/SlaveDriver-Engine's SCL_PriIntProc() (SCL_FUNC.C,
 * pinned a8986591557b6e680550d3c23970284d3b38ff8f, GPL-3.0-or-later): that
 * routine checks a handful of per-category SclPriBuffDirty.* flags once per
 * VBlank interval and DMA-copies only the categories flagged dirty, exactly
 * mirroring the "stage into a scratch buffer, flush only what changed at
 * one safe point" shape used below. No SlaveDriver source is copied --
 * that engine has no per-cell VDP2 tile/text dirty tracking to copy from;
 * this file's cell-level diff (a fixed array of (col,row,glyph) records
 * compared by key) is original engineering for this project. Full record
 * in docs/saturn/UPSTREAM_CODE_LEDGER.md ("Task 23A Task 6") and
 * docs/saturn/PROVENANCE.md. */
#include "saturn_hud_publish.h"

void
sm64_saturn_hud_publish_init(sm64_saturn_hud_publish_state_t *state)
{
    if (state == NULL)
        return;
    state->last_count = 0U;
    state->primed = 0;
}

/* Finds the entry in cells[0..count) at (col,row), or NULL if none. Linear
 * scan is O(count) per call and this function is called once per cell in
 * both passes of sm64_saturn_hud_publish() below, so a full publish costs
 * O(count^2) comparisons -- acceptable, not clever, because count is
 * bounded by SM64_SATURN_HUD_LAYOUT_MAX_CELLS (40, saturn_hud_layout.h),
 * so the worst case is a small, fixed number of uint8_t comparisons, not an
 * unbounded cost.
 *
 * Matching is by (col,row) only, deliberately ignoring glyph: this
 * function answers "is this cell still occupied at all," and callers
 * compare glyph separately once they already have the match. Task 5's
 * layout builder is documented (saturn_hud_layout.c's placement-derivation
 * comment) to never emit two cells at the same (col,row) for glyphs that
 * can be simultaneously visible in one snapshot, so duplicate (col,row)
 * keys are not an expected input to this function. If one ever occurred
 * anyway, returning the first match in array order is a harmless,
 * defensible choice -- equivalent to "whichever glyph the layout listed
 * first wins the cell" -- not a crash, an out-of-bounds access, or an
 * infinite loop; a real duplicate would still be a Task 5 layout bug to
 * fix there, not something this diff layer needs to detect or repair. */
static const sm64_saturn_hud_cell_t *
find_cell(const sm64_saturn_hud_cell_t *cells, uint32_t count, uint8_t col, uint8_t row)
{
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].col == col && cells[index].row == row)
            return &cells[index];
    }
    return NULL;
}

void
sm64_saturn_hud_publish(sm64_saturn_hud_publish_state_t *state,
                        const sm64_saturn_hud_snapshot_t *snapshot)
{
    if (state == NULL || snapshot == NULL)
        return;

    sm64_saturn_hud_cell_t next_cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t next_count = sm64_saturn_hud_layout_build(
        snapshot, next_cells, SM64_SATURN_HUD_LAYOUT_MAX_CELLS);

    /* Pass 1 -- clear vacated cells. A cell that was occupied last publish
     * and is not occupied this publish must be explicitly written as
     * SM64_SATURN_HUD_GLYPH_BLANK here: nothing else ever will, since
     * sm64_saturn_hud_layout_build() only emits cells that currently have a
     * glyph to show (saturn_hud_layout.h), never an explicit "now blank"
     * entry for a cell it has stopped using. */
    for (uint32_t index = 0U; index < state->last_count; index++) {
        const sm64_saturn_hud_cell_t *const prior = &state->last_cells[index];
        const sm64_saturn_hud_cell_t *const still_present =
            find_cell(next_cells, next_count, prior->col, prior->row);
        if (still_present == NULL)
            sm64_saturn_hud_atlas_write_cell(prior->col, prior->row, SM64_SATURN_HUD_GLYPH_BLANK);
    }

    /* Pass 2 -- write newly- or differently-occupied cells. A cell is
     * written when it is newly occupied (prior == NULL: absent from
     * last_cells) or was occupied with a different glyph (prior->glyph !=
     * next->glyph). The primed guard covers the very first call: last_count
     * is already 0 then (set by sm64_saturn_hud_publish_init()), so
     * find_cell() over an empty range would return NULL regardless -- the
     * guard just makes that "everything is new" behavior explicit rather
     * than incidental.
     *
     * No (col,row) can ever be written by both this pass and pass 1 above
     * in the same call: pass 1 only fires for (col,row) pairs absent from
     * next_cells, and pass 2 only ever iterates (col,row) pairs that ARE in
     * next_cells. Membership in next_cells partitions the two passes, so
     * they cannot race or double-write the same cell -- a glyph that
     * changes while staying occupied gets exactly the one write from this
     * pass, never a blank from pass 1 first. */
    for (uint32_t index = 0U; index < next_count; index++) {
        const sm64_saturn_hud_cell_t *const next = &next_cells[index];
        const sm64_saturn_hud_cell_t *const prior =
            state->primed
                ? find_cell(state->last_cells, state->last_count, next->col, next->row)
                : NULL;
        if (prior == NULL || prior->glyph != next->glyph)
            sm64_saturn_hud_atlas_write_cell(next->col, next->row, next->glyph);
    }

    for (uint32_t index = 0U; index < next_count; index++)
        state->last_cells[index] = next_cells[index];
    state->last_count = next_count;
    state->primed = 1;
}
