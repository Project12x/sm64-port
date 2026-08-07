/* src/port/saturn/gfx/saturn_hud_publish.h */
#ifndef SM64_SATURN_HUD_PUBLISH_H
#define SM64_SATURN_HUD_PUBLISH_H

#include "saturn_hud.h"
#include "saturn_hud_layout.h"

/* Remembers the layout actually written to the VDP2 atlas as of the last
 * sm64_saturn_hud_publish() call, so the next call can diff against it and
 * rewrite only what changed instead of the full grid every frame.
 *
 * last_cells/last_count mirror sm64_saturn_hud_layout_build()'s own
 * out_cells/count contract exactly (see saturn_hud_layout.h): only cells
 * that currently have a glyph to show are stored here, never an explicit
 * entry for an unoccupied cell. A cell's simple absence from this list
 * means "not occupied" -- that is what lets sm64_saturn_hud_publish()
 * detect "was occupied last publish, is not occupied this publish" as
 * "present in last_cells but absent from the new layout," with no sentinel
 * glyph value required.
 *
 * primed distinguishes "no layout has ever been published through this
 * state" (skip diffing entirely, write every cell of the first layout)
 * from "the last published layout legitimately had zero cells" (e.g. every
 * HUD display flag happened to be off that frame) -- last_count alone
 * cannot tell those two states apart, since both leave last_count == 0.
 * sm64_saturn_hud_publish_init() sets last_count and primed together, so
 * in practice primed == 0 implies last_count == 0 for any state reachable
 * through this API; the explicit primed check in the .c file is
 * belt-and-suspenders, not load-bearing beyond that first call. */
typedef struct sm64_saturn_hud_publish_state {
    sm64_saturn_hud_cell_t last_cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    uint32_t last_count;
    int primed;
} sm64_saturn_hud_publish_state_t;

void sm64_saturn_hud_publish_init(sm64_saturn_hud_publish_state_t *state);

/* Builds the layout for snapshot (via sm64_saturn_hud_layout_build(), Task
 * 5), diffs it against the layout last published through state, and calls
 * sm64_saturn_hud_atlas_write_cell() (Task 4) only for cells whose
 * (col,row,glyph) actually changed since then -- including cells that must
 * become SM64_SATURN_HUD_GLYPH_BLANK because they were occupied last
 * publish and are not occupied this publish. A cell whose glyph simply
 * changes (occupied both times, different glyph) gets exactly one write of
 * the new glyph, never a spurious blank-then-rewrite pair: see the two-pass
 * structure documented in saturn_hud_publish.c for why the "clear vacated
 * cells" pass and the "write occupied cells" pass can never both target the
 * same (col,row) in a single call.
 *
 * Pure logic: no VRAM access of its own. It only calls through the
 * write_cell function Task 4 provides; the host test links a test double
 * in its place (see tools/saturn/saturn_hud_layout_test.c), so this file
 * and its test never need Yaul headers. */
void sm64_saturn_hud_publish(sm64_saturn_hud_publish_state_t *state,
                             const sm64_saturn_hud_snapshot_t *snapshot);

#endif /* SM64_SATURN_HUD_PUBLISH_H */
