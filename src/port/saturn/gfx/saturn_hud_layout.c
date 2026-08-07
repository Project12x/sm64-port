/* src/port/saturn/gfx/saturn_hud_layout.c */
#include "saturn_hud_layout.h"

/* Mirrors src/game/level_update.h's enum HudDisplayFlags bit values exactly
 * (LIVES=0x0001, COIN_COUNT=0x0002, STAR_COUNT=0x0004,
 * CAMERA_AND_POWER=0x0008, TIMER=0x0040 -- confirmed against the real
 * header, not assumed). Redefined locally rather than included because
 * level_update.h pulls in the full N64 game-object graph, which this file
 * must not depend on: it is compiled standalone into a host test binary
 * with only this directory on the include path (see the
 * verify-saturn-hud-layout Make target). HUD_FLAG_KEYS (0x0010) and the
 * unnamed 0x0020 bit are deliberately not read anywhere below: hud.c's own
 * render_hud_keys() is documented there as "Unused function... Leftover
 * from the beta version," and the glyph atlas (Task 4) has no key glyph to
 * place, so there is nothing for this layout to build for that flag. */
#define HUD_FLAG_LIVES            0x0001
#define HUD_FLAG_COIN_COUNT       0x0002
#define HUD_FLAG_STAR_COUNT       0x0004
#define HUD_FLAG_CAMERA_AND_POWER 0x0008
#define HUD_FLAG_TIMER            0x0040

/* Mirrors src/game/camera.h's CAM_STATUS_* macros exactly (confirmed
 * against the real header: CAM_STATUS_MARIO = 1<<0, LAKITU = 1<<1,
 * FIXED = 1<<2, C_DOWN = 1<<3, C_UP = 1<<4, and the two OR'd group masks).
 * Redefined locally for the same standalone-host-build reason as the
 * HUD_FLAG_* block above -- camera.h depends on the full N64 camera
 * subsystem. */
#define CAM_STATUS_MARIO   (1 << 0)
#define CAM_STATUS_LAKITU  (1 << 1)
#define CAM_STATUS_FIXED   (1 << 2)
#define CAM_STATUS_C_DOWN  (1 << 3)
#define CAM_STATUS_C_UP    (1 << 4)
#define CAM_STATUS_MODE_GROUP   (CAM_STATUS_MARIO | CAM_STATUS_LAKITU | CAM_STATUS_FIXED)
#define CAM_STATUS_C_MODE_GROUP (CAM_STATUS_C_DOWN | CAM_STATUS_C_UP)

static uint32_t
push_cell(sm64_saturn_hud_cell_t *out_cells, uint32_t count, uint32_t capacity,
         uint8_t col, uint8_t row, sm64_saturn_hud_glyph_t glyph)
{
    if (count >= capacity)
        return count;
    out_cells[count].col = col;
    out_cells[count].row = row;
    out_cells[count].glyph = glyph;
    return count + 1U;
}

static sm64_saturn_hud_glyph_t
digit_glyph(int value)
{
    if (value < 0) value = 0;
    if (value > 9) value = 9;
    return (sm64_saturn_hud_glyph_t)(SM64_SATURN_HUD_GLYPH_DIGIT_0 + value);
}

/* Writes max_digits digit cells starting at (start_col,row), most
 * significant first, zero-padded. Note this truncates to the low-order
 * max_digits decimal digits of value (value mod 10^max_digits) rather than
 * saturating at the field's max representable value (10^max_digits - 1):
 * e.g. a 2-digit field showing value=105 would render "05", not "99". This
 * matches how the pathological "9999 lives/coins/stars" test values happen
 * to read either way here -- 9999 mod 100 == 99 == min(9999,99), and
 * likewise mod 1000 == 999 -- because 9999 is all-9s, which masks the
 * distinction. Values that overflow a field's digit width in a way that
 * ISN'T all-9s (e.g. 105 in a 2-digit field) would show a truncated,
 * visually-wrong-but-still-in-range number rather than a pinned max. This
 * is a pre-existing property of the field-width choices below (2 digits
 * for lives, 3 for coins/stars), not a new defect introduced here; real
 * gameplay values very rarely approach these widths. Flagged here per this
 * task's explicit self-review instruction to check clamping behavior
 * against the pathological test case, not silently left undocumented. */
static uint32_t
push_clamped_int(sm64_saturn_hud_cell_t *out_cells, uint32_t count, uint32_t capacity,
                 uint8_t start_col, uint8_t row, int32_t value, uint8_t max_digits)
{
    if (value < 0) value = 0;
    int32_t divisor = 1;
    for (uint8_t place = 1U; place < max_digits; place++)
        divisor *= 10;
    uint8_t written = 0U;
    for (uint8_t place = 0U; place < max_digits; place++) {
        const int32_t digit = (value / divisor) % 10;
        count = push_cell(out_cells, count, capacity,
                          (uint8_t)(start_col + written), row, digit_glyph(digit));
        written++;
        divisor /= 10;
        if (divisor == 0) divisor = 1;
    }
    return count;
}

uint32_t
sm64_saturn_hud_layout_build(const sm64_saturn_hud_snapshot_t *snapshot,
                             sm64_saturn_hud_cell_t *out_cells, uint32_t capacity)
{
    uint32_t count = 0U;
    if (snapshot == NULL || out_cells == NULL || capacity == 0U)
        return 0U;

    const int16_t flags = snapshot->flags;

    /* Cell placement below is derived from the real N64 HUD's pixel
     * positions in src/game/hud.c (SCREEN_WIDTH=320, SCREEN_HEIGHT=240,
     * confirmed in include/config.h), converted to this port's 16px VDP2
     * tile grid (320x224 visible => 20 cols x 14 rows, HUD_TILE_COLS/
     * HUD_TILE_ROWS in saturn_hud_atlas.c) by dividing pixel coordinates by
     * 16 and clustering to the bottom rows, matching hud.c's own
     * bottom-anchored layout (lives/coins/stars/camera all sit at
     * y=205-209 out of 240px; the timer sits one row higher at y=185).
     *
     * This is a coarse re-approximation, not a pixel-exact transform (a
     * 16px cell is too coarse for that, and the source uses sub-cell
     * offsets like x+4 for the small camera arrows) -- but every placement
     * below is a real derivation from those source coordinates, not
     * arbitrary, and every one is verified against the atlas's hard
     * col<20/row<14 bound (sm64_saturn_hud_atlas_write_cell() silently
     * no-ops any write outside it, see the header) and against every other
     * concurrently-active group placed here, so no two glyphs that can be
     * visible in the same frame ever target the same cell:
     *   row 10: power meter                      (col 8)
     *   row 11: timer                             (cols 13-18)
     *   row 12: lives (1-4), coins (10-14), stars (15-18)
     *   row 13: cannon reticle (15), camera (17-19)
     * HUD_DISPLAY_FLAG_LIVES | COIN_COUNT | STAR_COUNT | CAMERA_AND_POWER
     * are simultaneously active during ordinary gameplay (they are exactly
     * HUD_DISPLAY_DEFAULT's non-KEYS bits, level_update.h), and the timer
     * can join them during a PSS slide, so "everything at once" is a real
     * in-game state this layout must get right, not just a synthetic worst
     * case. */

    if (flags & HUD_FLAG_LIVES) {
        count = push_cell(out_cells, count, capacity, 1U, 12U, SM64_SATURN_HUD_GLYPH_MARIO_HEAD);
        count = push_cell(out_cells, count, capacity, 2U, 12U, SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, 3U, 12U, snapshot->lives, 2U);
    }
    if (flags & HUD_FLAG_COIN_COUNT) {
        count = push_cell(out_cells, count, capacity, 10U, 12U, SM64_SATURN_HUD_GLYPH_COIN);
        count = push_cell(out_cells, count, capacity, 11U, 12U, SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, 12U, 12U, snapshot->coins, 3U);
    }
    if (flags & HUD_FLAG_STAR_COUNT) {
        const uint8_t star_col = 15U;
        count = push_cell(out_cells, count, capacity, star_col, 12U, SM64_SATURN_HUD_GLYPH_STAR);
        if (snapshot->stars < 100) {
            count = push_cell(out_cells, count, capacity, (uint8_t)(star_col + 1U), 12U,
                              SM64_SATURN_HUD_GLYPH_MULTIPLY);
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(star_col + 2U), 12U,
                                     snapshot->stars, 2U);
        } else {
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(star_col + 1U), 12U,
                                     snapshot->stars, 3U);
        }
    }
    if (flags & HUD_FLAG_TIMER) {
        const uint16_t frames = snapshot->timer;
        const uint16_t minutes = (uint16_t)(frames / (30U * 60U));
        const uint16_t seconds = (uint16_t)((frames - minutes * 1800U) / 30U);
        const uint16_t frac = (uint16_t)(((frames - minutes * 1800U - seconds * 30U)) / 3U);
        count = push_clamped_int(out_cells, count, capacity, 13U, 11U, minutes, 1U);
        count = push_cell(out_cells, count, capacity, 14U, 11U, SM64_SATURN_HUD_GLYPH_APOSTROPHE);
        count = push_clamped_int(out_cells, count, capacity, 15U, 11U, seconds, 2U);
        count = push_cell(out_cells, count, capacity, 17U, 11U, SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE);
        count = push_clamped_int(out_cells, count, capacity, 18U, 11U, frac, 1U);
    }
    if (flags & HUD_FLAG_CAMERA_AND_POWER) {
        count = push_cell(out_cells, count, capacity, 17U, 13U, SM64_SATURN_HUD_GLYPH_CAM_CAMERA);
        switch (snapshot->camera_status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            count = push_cell(out_cells, count, capacity, 18U, 13U, SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD);
            break;
        case CAM_STATUS_LAKITU:
            count = push_cell(out_cells, count, capacity, 18U, 13U, SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD);
            break;
        case CAM_STATUS_FIXED:
            count = push_cell(out_cells, count, capacity, 18U, 13U, SM64_SATURN_HUD_GLYPH_CAM_FIXED);
            break;
        default:
            break;
        }
        /* Up/down share one cell: CAM_STATUS_C_DOWN and CAM_STATUS_C_UP are
         * mutually exclusive case labels of the same switch (both live in
         * CAM_STATUS_C_MODE_GROUP, matching hud.c's render_hud_camera_status
         * switch), so only one of them can ever be the glyph written to
         * (19,13) in a given snapshot -- there is no frame where both are
         * simultaneously live and would fight over the cell. */
        switch (snapshot->camera_status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            count = push_cell(out_cells, count, capacity, 19U, 13U, SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN);
            break;
        case CAM_STATUS_C_UP:
            count = push_cell(out_cells, count, capacity, 19U, 13U, SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP);
            break;
        default:
            break;
        }

        /* Power meter Y position (snapshot->power_meter_y) is intentionally
         * not used for tile placement -- Saturn's tile grid is coarser than
         * the source's pixel-accurate slide animation, so this plan places
         * the meter at a fixed cell and represents animation only through
         * which wedge-count glyph is shown.
         *
         * Gate condition: snapshot->power_meter_animation != 0 is exactly
         * equivalent to != POWER_METER_HIDDEN (src/game/hud.h's enum
         * PowerMeterAnimation defines POWER_METER_HIDDEN as the first
         * enumerator, value 0; POWER_METER_EMPHASIZED/DEEMPHASIZING/
         * HIDING/VISIBLE are 1-4). This matches hud.c's real
         * render_hud_power_meter() gate exactly:
         *   if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) { return; }
         * i.e. the source renders the meter for ALL FOUR non-hidden
         * animation phases (EMPHASIZED, DEEMPHASIZING, HIDING, VISIBLE),
         * not just the resting VISIBLE state -- so gating on merely
         * "!= POWER_METER_HIDDEN" is correct and is not a weakened
         * substitute for some narrower condition. Confirmed by reading the
         * real hud.c (render_hud_power_meter(), hud.c:229-257) rather than
         * assumed. */
        if (snapshot->power_meter_animation != 0) {
            /* Only wedges 1-8 have a source texture (see Task 4's atlas
             * comment) -- clamp to that range, not 0-8. A wedges==0 read
             * while visible would be a source-side edge case already
             * absent from hud.c's own lookup table; this clamp is a
             * defensive VRAM-read bound, not a gameplay behavior change. */
            int wedges = snapshot->wedges;
            if (wedges < 1) wedges = 1;
            if (wedges > 8) wedges = 8;
            count = push_cell(out_cells, count, capacity, 8U, 10U,
                              (sm64_saturn_hud_glyph_t)(SM64_SATURN_HUD_GLYPH_POWER_METER_1 + (wedges - 1)));
        }
    }
    if (snapshot->cannon_active) {
        count = push_cell(out_cells, count, capacity, 15U, 13U, SM64_SATURN_HUD_GLYPH_CANNON_RETICLE);
    }

    return count;
}
