/* src/port/saturn/gfx/saturn_hud_layout.c */
/* NULL (used below in sm64_saturn_hud_layout_build()'s argument guard) is
 * not guaranteed by saturn_hud_layout.h's own <stdint.h> include -- the C
 * standard does not require <stdint.h> to define it. Host gcc's headers
 * happened to pull NULL in transitively regardless, which let this compile
 * on host (verify-saturn-hud-layout) while failing the real SH-2 cross
 * compiler's stricter newlib headers ("'NULL' undeclared"), found by Task 9
 * during this plan's first full target build. */
#include <stddef.h>

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

/* Row assignments for the four tile rows this layout uses (of the atlas's
 * 14 visible rows, 0-13). Named -- rather than left as bare numbers at each
 * push_cell() call site -- so a future edit that moves one group can't
 * silently reintroduce a collision with another group without at least
 * changing a visible, greppable identifier; the compiler can't catch a
 * collision either way (two groups landing on the same cell is a logical
 * error, not a type error), but a named constant makes the intent legible
 * and makes cross-group collisions grep-auditable. See the placement-
 * derivation comment on sm64_saturn_hud_layout_build() below for why these
 * four rows were chosen.
 *
 * Corrected 2026-08-09 (owner manual-screenshot review of the first working
 * render, commit a6c2032a, caught the HUD drawing lives/coins/stars/timer
 * at the BOTTOM of the frame -- the opposite of the source game, which
 * draws them near the TOP). HUD_ROW_COUNTERS and HUD_ROW_TIMER move from
 * the bottom cluster (12, 11) to the top (0, 2); HUD_ROW_POWER_METER moves
 * from the bottom cluster (10) to share the timer's row (2). All three are
 * genuinely near-top in the source once the real rendering paths are
 * traced pixel-by-pixel (see the placement-derivation comment below) --
 * this corrects the 2026-08-07 code-review pass, which traced the same
 * flip correctly but only fixed the comment, not these constants.
 * HUD_ROW_CANNON_CAMERA is unchanged: the camera icon's source position
 * genuinely is bottom-anchored, and this fix's scope does not touch it. */
#define HUD_ROW_COUNTERS      0U  /* lives, coins, stars all share this row */
#define HUD_ROW_TIMER         2U
#define HUD_ROW_POWER_METER   2U  /* shares the timer's row -- see below for why this is pixel-accurate, not sloppy */
#define HUD_ROW_CANNON_CAMERA 13U /* cannon reticle and camera share this row (unchanged by this fix) */

/* Column where each group's cluster starts; every glyph within a group is
 * placed at a small fixed offset from its group's start column (see each
 * block in sm64_saturn_hud_layout_build() below), the same way the star-
 * count block's own offsets already worked before this refactor. Named for
 * the same collision-auditing reason as the HUD_ROW_* constants above. */
#define HUD_COL_POWER_METER 8U
#define HUD_COL_LIVES       1U
#define HUD_COL_COINS       10U
#define HUD_COL_STARS       15U
#define HUD_COL_TIMER       13U
#define HUD_COL_CANNON      15U
#define HUD_COL_CAMERA      17U

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
    /* Every call site below passes max_digits of 2 or 3, so the general loop
     * that follows never runs on this build -- but it is retained rather than
     * asserted away so this function stays total over its whole declared
     * domain, and so the ladder can be proven bit-identical against it rather
     * than merely believed to be.
     *
     * Why the ladder exists: `value / divisor` with a *runtime* divisor is a
     * call to ___sdivsi3, GCC's 32-step software divide, and the loop makes
     * `max_digits` of them per call across seven call sites per HUD build.
     * With the divisor a compile-time constant GCC strength-reduces each
     * division to a reciprocal multiply through `dmuls.l`, so the divide
     * helper disappears entirely. */
    if (max_digits <= 3U) {
        uint8_t written = 0U;
        if (max_digits >= 3U) {
            count = push_cell(out_cells, count, capacity,
                              (uint8_t)(start_col + written), row,
                              digit_glyph((value / 100) % 10));
            written++;
        }
        if (max_digits >= 2U) {
            count = push_cell(out_cells, count, capacity,
                              (uint8_t)(start_col + written), row,
                              digit_glyph((value / 10) % 10));
            written++;
        }
        if (max_digits >= 1U) {
            count = push_cell(out_cells, count, capacity,
                              (uint8_t)(start_col + written), row,
                              digit_glyph(value % 10));
        }
        return count;
    }
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

    /* Placement derivation (corrected 2026-08-09; re-derived fresh from
     * the real source, not copied from the prior pass's summary -- see the
     * HUD_ROW_* block above for why this correction happened). Every
     * group's row below is the result of tracing its actual rendering path
     * in src/game/hud.c and src/game/print.c and converting the real,
     * final on-screen pixel row into this port's 14-row tile grid (16px
     * per row, matching the 224px-tall region both source paths below
     * ultimately draw into).
     *
     * Two DIFFERENT rendering paths exist in the source, and they use two
     * DIFFERENT Y conventions -- this is the whole reason the original
     * 2026-08-06 pass got this wrong:
     *
     * (1) Lives, coins, stars, and the timer render through
     * print_text()/print_text_fmt_int() -> render_text_labels() ->
     * render_textrect() (src/game/print.c:389-403), which unconditionally
     * flips the Y axis before issuing a raw gSPTextureRectangle:
     * `s32 rectBaseY = 224 - y;` (print.c:391, confirmed by reading the
     * real file). gSPTextureRectangle is a raw RDP screen-space command,
     * not subject to the active projection/modelview matrix -- which is
     * exactly why print.c must do this flip itself instead of relying on
     * one. HUD_TOP_Y (209, used by lives/coins/stars, hud.c) flips to
     * real screen y = 224-209 = 15 -> tile row floor(15/16) = 0. The
     * timer's y=185 (render_hud_timer, hud.c) flips to real screen
     * y = 224-185 = 39 -> tile row floor(39/16) = 2. (Corroborated
     * independently: the timer's own apostrophe/double-quote glyphs draw
     * via the *unflipped* render_hud_tex_lut() path at a hardcoded y=32,
     * which is tile row floor(32/16) = 2 -- the same row as the flipped
     * digit text, as two pieces of one on-screen readout must be.)
     *
     * (2) The camera status icon (render_hud_camera_status(), hud.c) also
     * uses the unflipped render_hud_tex_lut() path, directly at y=205 --
     * no transform, so y=205 really is near the bottom: tile row
     * floor(205/16) = 12. This implementation places it at row 13 (the
     * grid's bottom-most row, one below its literal derived row) so it
     * sits flush with the cannon reticle in the same corner -- a
     * deliberate grid-coarseness rounding, unchanged by this fix and out
     * of its scope (kept exactly as HUD_ROW_CANNON_CAMERA already was).
     *
     * (3) The power meter (health wheel) is neither of the above: it
     * renders as real transformed geometry, not a raw texture rectangle.
     * render_dl_power_meter() (hud.c) positions it with
     * `guTranslate(mtx, sPowerMeterHUD.x, sPowerMeterHUD.y, 0)` inside the
     * SAME shared HUD projection render_hud() sets up once per frame via
     * create_dl_ortho_matrix() (src/game/ingame_menu.c:183-195):
     * `guOrtho(matrix, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, -10.0f,
     * 10.0f, 1.0f)` -- a Y-UP world convention (bottom=0, top=SCREEN_HEIGHT)
     * using the literal SCREEN_HEIGHT macro (240, include/config.h), NOT
     * print.c's own "224" flip constant -- these are two different
     * numbers in the real source, not a typo in this comment. Because this
     * path is a real vertex transform, its final raw screen row is
     * `SCREEN_HEIGHT - y = 240 - y`. sPowerMeterHUD.y animates from 166
     * (just took damage, POWER_METER_EMPHASIZED) to its resting/visible
     * value of 200 (POWER_METER_VISIBLE, animate_power_meter_deemphasizing,
     * hud.c) -- and, once healed, keeps climbing past 224 during
     * POWER_METER_HIDING (animate_power_meter_hiding: y += 20 until
     * y >= 301) to genuinely leave the top of the screen, matching the
     * well-known real-game behavior of the health wheel sliding UP and
     * off-screen when Mario is at full health. At its typical
     * resting/visible position (y=200 -- what's on screen the overwhelming
     * majority of the time it's shown; EMPHASIZED/DEEMPHASIZING are ~45-
     * frame transients), real screen row = 240-200 = 40 -> tile row
     * floor(40/16) = 2 -- the SAME tile row as the timer (39px vs 40px
     * from the top of the screen: the two are genuinely almost the same
     * height in the source). HUD_COL_POWER_METER (8, a single fixed cell)
     * and HUD_COL_TIMER (13, spanning cols 13-18) never overlap, so
     * sharing HUD_ROW_TIMER's row number is the pixel-accurate placement,
     * not a shortcut. snapshot->power_meter_y still is not read for tile
     * placement -- same reasoning as before this fix: the 16px grid can't
     * represent the sub-tile slide animation, and this layout already
     * represents the animation through which wedge-count glyph is shown,
     * not through cell position -- only the FIXED row this static cell
     * targets has been corrected (10 -> 2).
     *
     * Net result: three of the four groups (counters, timer, power meter)
     * cluster near the TOP of the screen, matching the source; only the
     * camera icon (and the cannon reticle placed alongside it) stays near
     * the bottom, because that is where the source genuinely puts it.
     *
     * Every cell below is verified against the atlas's hard col<20/row<14
     * bound (sm64_saturn_hud_atlas_write_cell() silently no-ops any write
     * outside it, see the header) and against every other concurrently-
     * active group placed here, so no two glyphs that can be visible in
     * the same frame ever target the same cell:
     *   HUD_ROW_COUNTERS      (0):  lives (1-4), coins (10-14), stars (15-18)
     *   HUD_ROW_TIMER         (2):  timer                     (cols 13-18)
     *   HUD_ROW_POWER_METER   (2):  power meter               (col 8)
     *   HUD_ROW_CANNON_CAMERA (13): cannon reticle (15), camera (17-19)
     * HUD_DISPLAY_FLAG_LIVES | COIN_COUNT | STAR_COUNT | CAMERA_AND_POWER
     * are simultaneously active during ordinary gameplay (they are exactly
     * HUD_DISPLAY_DEFAULT's non-KEYS bits, level_update.h), and the timer
     * can join them during a PSS slide, so "everything at once" is a real
     * in-game state this layout must get right, not just a synthetic worst
     * case. See test_layout_no_collisions_across_realistic_snapshots
     * (tools/saturn/saturn_hud_layout_test.c) for the systematic
     * (col,row) collision check across every group combination that can
     * realistically co-occur, added alongside this fix. */

    if (flags & HUD_FLAG_LIVES) {
        count = push_cell(out_cells, count, capacity, HUD_COL_LIVES, HUD_ROW_COUNTERS,
                          SM64_SATURN_HUD_GLYPH_MARIO_HEAD);
        count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_LIVES + 1U), HUD_ROW_COUNTERS,
                          SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_LIVES + 2U), HUD_ROW_COUNTERS,
                                 snapshot->lives, 2U);
    }
    if (flags & HUD_FLAG_COIN_COUNT) {
        count = push_cell(out_cells, count, capacity, HUD_COL_COINS, HUD_ROW_COUNTERS,
                          SM64_SATURN_HUD_GLYPH_COIN);
        count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_COINS + 1U), HUD_ROW_COUNTERS,
                          SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_COINS + 2U), HUD_ROW_COUNTERS,
                                 snapshot->coins, 3U);
    }
    if (flags & HUD_FLAG_STAR_COUNT) {
        count = push_cell(out_cells, count, capacity, HUD_COL_STARS, HUD_ROW_COUNTERS,
                          SM64_SATURN_HUD_GLYPH_STAR);
        /* Covered by test_layout_star_count_below_100_uses_two_digit_field
         * (tools/saturn/saturn_hud_layout_test.c, added in Task 7), which
         * mutation-verified this branch: forcing the >=100 path
         * unconditionally makes that test fail (no multiply glyph, 3 digit
         * cells instead of 2). test_layout_never_exceeds_capacity's
         * stars=9999 already covered the >=100 sibling branch. */
        if (snapshot->stars < 100) {
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_STARS + 1U), HUD_ROW_COUNTERS,
                              SM64_SATURN_HUD_GLYPH_MULTIPLY);
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_STARS + 2U), HUD_ROW_COUNTERS,
                                     snapshot->stars, 2U);
        } else {
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_STARS + 1U), HUD_ROW_COUNTERS,
                                     snapshot->stars, 3U);
        }
    }
    if (flags & HUD_FLAG_TIMER) {
        const uint16_t frames = snapshot->timer;
        const uint16_t minutes = (uint16_t)(frames / (30U * 60U));
        const uint16_t seconds = (uint16_t)((frames - minutes * 1800U) / 30U);
        const uint16_t frac = (uint16_t)(((frames - minutes * 1800U - seconds * 30U)) / 3U);
        count = push_clamped_int(out_cells, count, capacity, HUD_COL_TIMER, HUD_ROW_TIMER, minutes, 1U);
        count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_TIMER + 1U), HUD_ROW_TIMER,
                          SM64_SATURN_HUD_GLYPH_APOSTROPHE);
        count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_TIMER + 2U), HUD_ROW_TIMER,
                                 seconds, 2U);
        count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_TIMER + 4U), HUD_ROW_TIMER,
                          SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE);
        count = push_clamped_int(out_cells, count, capacity, (uint8_t)(HUD_COL_TIMER + 5U), HUD_ROW_TIMER,
                                 frac, 1U);
    }
    if (flags & HUD_FLAG_CAMERA_AND_POWER) {
        count = push_cell(out_cells, count, capacity, HUD_COL_CAMERA, HUD_ROW_CANNON_CAMERA,
                          SM64_SATURN_HUD_GLYPH_CAM_CAMERA);
        /* Covered by test_layout_camera_mode_switch_selects_correct_glyph
         * (tools/saturn/saturn_hud_layout_test.c, added in Task 7), which
         * mutation-verified all three case labels: swapping two cases'
         * glyphs makes that test fail. */
        switch (snapshot->camera_status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_CAMERA + 1U), HUD_ROW_CANNON_CAMERA,
                              SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD);
            break;
        case CAM_STATUS_LAKITU:
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_CAMERA + 1U), HUD_ROW_CANNON_CAMERA,
                              SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD);
            break;
        case CAM_STATUS_FIXED:
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_CAMERA + 1U), HUD_ROW_CANNON_CAMERA,
                              SM64_SATURN_HUD_GLYPH_CAM_FIXED);
            break;
        default:
            break;
        }
        /* Up/down share one cell: CAM_STATUS_C_DOWN and CAM_STATUS_C_UP are
         * mutually exclusive case labels of the same switch (both live in
         * CAM_STATUS_C_MODE_GROUP, matching hud.c's render_hud_camera_status
         * switch), so only one of them can ever be the glyph written to
         * (HUD_COL_CAMERA+2, HUD_ROW_CANNON_CAMERA) in a given snapshot --
         * there is no frame where both are simultaneously live and would
         * fight over the cell.
         * Covered by test_layout_camera_cbutton_switch_selects_correct_glyph
         * (tools/saturn/saturn_hud_layout_test.c, added in Task 7), which
         * mutation-verified both case labels: swapping their glyphs makes
         * that test fail. */
        switch (snapshot->camera_status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_CAMERA + 2U), HUD_ROW_CANNON_CAMERA,
                              SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN);
            break;
        case CAM_STATUS_C_UP:
            count = push_cell(out_cells, count, capacity, (uint8_t)(HUD_COL_CAMERA + 2U), HUD_ROW_CANNON_CAMERA,
                              SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP);
            break;
        default:
            break;
        }

        /* Power meter Y position (snapshot->power_meter_y) is intentionally
         * not used for tile placement -- Saturn's tile grid is coarser than
         * the source's pixel-accurate slide animation, so this plan places
         * the meter at a fixed cell and represents animation only through
         * which wedge-count glyph is shown. That fixed cell's row was
         * HUD_ROW_POWER_METER=10 (bottom cluster) until 2026-08-09, when it
         * was corrected to HUD_ROW_POWER_METER=2 to match the meter's real
         * near-top settled screen position; see the placement-derivation
         * comment above sm64_saturn_hud_layout_build() for the pixel math.
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
            count = push_cell(out_cells, count, capacity, HUD_COL_POWER_METER, HUD_ROW_POWER_METER,
                              (sm64_saturn_hud_glyph_t)(SM64_SATURN_HUD_GLYPH_POWER_METER_1 + (wedges - 1)));
        }
    }
    /* Covered by test_layout_places_cannon_reticle_when_active and
     * test_layout_omits_cannon_reticle_when_inactive
     * (tools/saturn/saturn_hud_layout_test.c, added in Task 7), which
     * mutation-verified this gate in both directions: inverting it makes
     * both tests fail. */
    if (snapshot->cannon_active) {
        count = push_cell(out_cells, count, capacity, HUD_COL_CANNON, HUD_ROW_CANNON_CAMERA,
                          SM64_SATURN_HUD_GLYPH_CANNON_RETICLE);
    }

    return count;
}
