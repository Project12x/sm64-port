/* src/port/saturn/gfx/saturn_hud_layout.h */
#ifndef SM64_SATURN_HUD_LAYOUT_H
#define SM64_SATURN_HUD_LAYOUT_H

#include <stdint.h>

#include "saturn_hud.h"
#include "saturn_hud_atlas.h"

/* Exhaustively traced against every branch of sm64_saturn_hud_layout_build()
 * (2026-08-07 memory-budget audit): LIVES=4, COIN_COUNT=5, STAR_COUNT=4,
 * TIMER=6, CAMERA_AND_POWER=4, CANNON=1 -- all of LIVES/COIN_COUNT/
 * STAR_COUNT/CAMERA_AND_POWER are simultaneously active in ordinary
 * gameplay and TIMER can join during a PSS slide, so 4+5+4+6+4+1=24 is the
 * real worst case given the current source, not a rounded guess. Every
 * push_cell()/push_clamped_int() call site was counted, not sampled. */
#define SM64_SATURN_HUD_LAYOUT_MAX_CELLS 24U

typedef struct sm64_saturn_hud_cell {
    uint8_t col;
    uint8_t row;
    sm64_saturn_hud_glyph_t glyph;
} sm64_saturn_hud_cell_t;

/* Decides which glyph belongs in which tile cell for one HUD snapshot.
 * Pure function: no VRAM access, fully host-testable. Writes at most
 * capacity cells into out_cells and returns the count actually written
 * (never more than capacity, even for pathological snapshot values).
 *
 * Every (col,row) this function can ever emit satisfies col < 20 and
 * row < 14 -- the real visible-grid bound sm64_saturn_hud_atlas_write_cell()
 * enforces (src/port/saturn/gfx/saturn_hud_atlas.c: HUD_TILE_COLS/
 * HUD_TILE_ROWS, silently no-ops any write outside that range). Cells
 * placed outside that bound would never fail a test in this file -- none
 * of the tests inspect col/row, only glyph presence -- but would render as
 * a permanently invisible HUD element on real hardware. saturn_hud_layout.c
 * documents the derivation of every placement against that bound. */
uint32_t sm64_saturn_hud_layout_build(const sm64_saturn_hud_snapshot_t *snapshot,
                                      sm64_saturn_hud_cell_t *out_cells,
                                      uint32_t capacity);

#endif /* SM64_SATURN_HUD_LAYOUT_H */
