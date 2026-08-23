/* src/port/saturn/gfx/saturn_hud_atlas.h */
#ifndef SM64_SATURN_HUD_ATLAS_H
#define SM64_SATURN_HUD_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

/* One entry per distinct glyph the HUD can display. Order matches the
 * character-pattern slots written into VRAM by
 * sm64_saturn_hud_atlas_init() -- index IS the VDP2 character-pattern
 * number, so this enum's order must never change without also touching
 * the atlas init loop. */
typedef enum sm64_saturn_hud_glyph {
    SM64_SATURN_HUD_GLYPH_DIGIT_0,
    SM64_SATURN_HUD_GLYPH_DIGIT_1,
    SM64_SATURN_HUD_GLYPH_DIGIT_2,
    SM64_SATURN_HUD_GLYPH_DIGIT_3,
    SM64_SATURN_HUD_GLYPH_DIGIT_4,
    SM64_SATURN_HUD_GLYPH_DIGIT_5,
    SM64_SATURN_HUD_GLYPH_DIGIT_6,
    SM64_SATURN_HUD_GLYPH_DIGIT_7,
    SM64_SATURN_HUD_GLYPH_DIGIT_8,
    SM64_SATURN_HUD_GLYPH_DIGIT_9,
    SM64_SATURN_HUD_GLYPH_MULTIPLY,
    SM64_SATURN_HUD_GLYPH_COIN,
    SM64_SATURN_HUD_GLYPH_MARIO_HEAD,
    SM64_SATURN_HUD_GLYPH_STAR,
    SM64_SATURN_HUD_GLYPH_APOSTROPHE,
    SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE,
    SM64_SATURN_HUD_GLYPH_CAM_CAMERA,
    SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD,
    SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD,
    SM64_SATURN_HUD_GLYPH_CAM_FIXED,
    SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP,
    SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN,
    SM64_SATURN_HUD_GLYPH_POWER_METER_1,
    SM64_SATURN_HUD_GLYPH_POWER_METER_2,
    SM64_SATURN_HUD_GLYPH_POWER_METER_3,
    SM64_SATURN_HUD_GLYPH_POWER_METER_4,
    SM64_SATURN_HUD_GLYPH_POWER_METER_5,
    SM64_SATURN_HUD_GLYPH_POWER_METER_6,
    SM64_SATURN_HUD_GLYPH_POWER_METER_7,
    SM64_SATURN_HUD_GLYPH_POWER_METER_8,
    SM64_SATURN_HUD_GLYPH_CANNON_RETICLE,
    /* S4.2: the HUD alphabet. Twenty-one letters, not twenty-six -- J, Q, V,
     * X and Z exist only in the JP/SH ROMs (bin/segment2.c guards them behind
     * VERSION_JP/EU/SH and assets.json has no "us" region for them), so text
     * drawn through this atlas cannot contain them. That is a real constraint
     * on wording, not a gap to paper over: sm64_saturn_hud_glyph_for_char()
     * returns COUNT for an unavailable character so the caller can decide,
     * rather than silently dropping it. */
    SM64_SATURN_HUD_GLYPH_LETTER_A,
    SM64_SATURN_HUD_GLYPH_LETTER_B,
    SM64_SATURN_HUD_GLYPH_LETTER_C,
    SM64_SATURN_HUD_GLYPH_LETTER_D,
    SM64_SATURN_HUD_GLYPH_LETTER_E,
    SM64_SATURN_HUD_GLYPH_LETTER_F,
    SM64_SATURN_HUD_GLYPH_LETTER_G,
    SM64_SATURN_HUD_GLYPH_LETTER_H,
    SM64_SATURN_HUD_GLYPH_LETTER_I,
    SM64_SATURN_HUD_GLYPH_LETTER_K,
    SM64_SATURN_HUD_GLYPH_LETTER_L,
    SM64_SATURN_HUD_GLYPH_LETTER_M,
    SM64_SATURN_HUD_GLYPH_LETTER_N,
    SM64_SATURN_HUD_GLYPH_LETTER_O,
    SM64_SATURN_HUD_GLYPH_LETTER_P,
    SM64_SATURN_HUD_GLYPH_LETTER_R,
    SM64_SATURN_HUD_GLYPH_LETTER_S,
    SM64_SATURN_HUD_GLYPH_LETTER_T,
    SM64_SATURN_HUD_GLYPH_LETTER_U,
    SM64_SATURN_HUD_GLYPH_LETTER_W,
    SM64_SATURN_HUD_GLYPH_LETTER_Y,
    SM64_SATURN_HUD_GLYPH_BLANK, /* solid transparent: clears a dirty cell */
    SM64_SATURN_HUD_GLYPH_COUNT
} sm64_saturn_hud_glyph_t;

/* Uploads every character pattern once and configures the NBG0 cell plane.
 * Must run after source_cart_load() (glyph pixels are .cart_rodata-linked,
 * same constraint sourceboot_init_sky_bitmap already documents for NBG1)
 * and before the first sm64_saturn_hud_atlas_write_cell() call. */
void sm64_saturn_hud_atlas_init(void);

/* Writes one 16x16 character cell at tile-grid position (col,row) (0-based).
 * The visible HUD region is 20 columns x 14 rows of 16x16 cells: this port
 * runs VDP2 at 320x224 (VDP2_TVMD_HORZ_NORMAL_A / VDP2_TVMD_VERT_224, set in
 * sourceboot's user_init(), src/port/saturn/sourceboot/main.c:1512-1514),
 * and 320/16 = 20, 224/16 = 14. This is smaller than the underlying
 * VDP2_SCRN_PLANE_SIZE_1X1 page, which is a fixed 32x32-cell grid for
 * CHAR_SIZE_2X2 regardless of screen resolution (VDP2_SCRN_PAGE_WIDTH/
 * HEIGHT_CALCULATE in scrn_macros.h) -- cells at col>=20 or row>=14 are
 * real, addressable PND entries, they are just outside the visible raster
 * at this resolution, so writes there are rejected as a no-op rather than
 * silently wasted on something nothing will ever display. */
void sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row,
                                      sm64_saturn_hud_glyph_t glyph);

/* Visible cell grid at this port's 320x224 VDP2 mode. */
#define SM64_SATURN_HUD_COLS 20U
#define SM64_SATURN_HUD_ROWS 14U

/* Maps one ASCII character onto a glyph. Lower case folds to upper. Returns
 * SM64_SATURN_HUD_GLYPH_COUNT for anything the US atlas cannot draw --
 * including J, Q, V, X and Z -- so callers can fail loudly instead of
 * rendering a hole. Space maps to BLANK. */
sm64_saturn_hud_glyph_t sm64_saturn_hud_glyph_for_char(char value);

/* Draws `text` starting at (col,row), clipped to the visible grid. Returns
 * false if any character had no glyph, having drawn the rest -- a caller
 * choosing wording for this alphabet wants to know at the point of use. */
bool sm64_saturn_hud_atlas_write_string(uint8_t col, uint8_t row,
                                        const char *text);

/* Fills a rectangle with one glyph; BLANK is how a panel is erased. */
void sm64_saturn_hud_atlas_fill(uint8_t col, uint8_t row,
                                uint8_t cols, uint8_t rows,
                                sm64_saturn_hud_glyph_t glyph);

#endif /* SM64_SATURN_HUD_ATLAS_H */
