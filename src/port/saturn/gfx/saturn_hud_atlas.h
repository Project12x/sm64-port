/* src/port/saturn/gfx/saturn_hud_atlas.h */
#ifndef SM64_SATURN_HUD_ATLAS_H
#define SM64_SATURN_HUD_ATLAS_H

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

#endif /* SM64_SATURN_HUD_ATLAS_H */
