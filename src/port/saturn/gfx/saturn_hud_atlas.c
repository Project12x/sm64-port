/* src/port/saturn/gfx/saturn_hud_atlas.c */
#include "saturn_hud_atlas.h"

#include <yaul.h>

#include "saturn_hud_glyphs_generated.h"

/* Bank 2 (VDP2 quarter-bank B0), NOT bank 1 -- found and fixed during Task 8
 * integration. VDP2_VRAM_ADDR(bank, offset) addresses one of 4 physical
 * 0x20000-byte quarter-banks (A0=0, A1=1, B0=2, B1=3; see the diagram in
 * third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/vram.h). This plan's design
 * decision #7 called bank 1 "bank B0, disjoint from NBG1's sky bitmap in
 * bank A0" -- but sourceboot's sky bitmap is 512x256 @ RGB1555
 * (sourceboot_init_sky_bitmap(), main.c) = 0x40000 bytes = exactly TWO
 * quarter-banks, so it physically occupies bank A0 AND bank A1 in full
 * (0x25E00000-0x25E3FFFF). Bank 1 in VDP2_VRAM_ADDR's real numbering IS
 * bank A1 (0x25E20000-0x25E3FFFF) -- the second half of the sky bitmap, not
 * a disjoint region. Placing the HUD atlas there would have had
 * sm64_saturn_hud_atlas_init() overwrite the sky bitmap's bottom 128 rows
 * (VRAM offsets 0x00000-0x08800 within A1) the moment Task 8 wired the atlas
 * init call into boot -- a real corruption, not a bandwidth question, and
 * cycle-pattern slot allocation cannot fix a base-address collision. Bank 2
 * (B0) is confirmed unused by anything else in this target (sky bitmap:
 * banks 0-1; dbgio's NBG3 console + the backscreen gradient table: both
 * bank 3), so this is the actual disjoint region the plan intended. */
#define HUD_CPD_BASE VDP2_VRAM_ADDR(2, 0x00000)
#define HUD_PND_BASE VDP2_VRAM_ADDR(2, 0x08000)

/* Real VDP2 page geometry for CHAR_SIZE_2X2 + PLANE_SIZE_1X1: a page is
 * always a 32x32 cell grid (VDP2_SCRN_PAGE_WIDTH_CALCULATE /
 * VDP2_SCRN_PAGE_HEIGHT_CALCULATE in scrn_macros.h both return 32 for
 * CHAR_SIZE_2X2, independent of TV resolution). This is the true PND row
 * stride in VRAM and must not be confused with HUD_TILE_COLS/HUD_TILE_ROWS
 * below, which describe how much of that page is actually on screen. */
#define HUD_PAGE_STRIDE_COLS 32U

/* Visible HUD region at this port's 320x224 VDP2 TV mode: see the doc
 * comment on sm64_saturn_hud_atlas_write_cell() in the header for the full
 * derivation (320/16, 224/16). */
#define HUD_TILE_COLS 20U
#define HUD_TILE_ROWS 14U

#define HUD_CHAR_DIM 16U /* character-pattern width/height in texels */
#define HUD_CHAR_BYTES (HUD_CHAR_DIM * HUD_CHAR_DIM * 2U) /* RGB1555 texels per pattern */

/* Guards against a future glyph addition silently growing the character-
 * pattern data past HUD_PND_BASE and corrupting the pattern-name table's
 * own VRAM region -- the same invariant-enforcement shape already used by
 * src/port/saturn/audio/saturn_pcm_protocol.h:164
 * (SM64_SATURN_PCM_BANK_OFFSET < SM64_SATURN_PCM_SOUND_RAM_BYTES). Currently
 * 32 glyphs * 512 bytes = 16384, comfortably under the 32768-byte gap to
 * HUD_PND_BASE. */
_Static_assert((uint32_t)SM64_SATURN_HUD_GLYPH_COUNT * HUD_CHAR_BYTES <=
               (HUD_PND_BASE - HUD_CPD_BASE),
               "HUD character-pattern data overflows into the PND region");

/* Uploads a source glyph image into one 16x16 (CHAR_SIZE_2X2) character
 * pattern slot.
 *
 * VDP2 hardware does not store a 2x2-cell pattern as one flat 16-wide
 * raster: it is four separately-addressed, individually-contiguous 8x8
 * cells, ordered top-left, top-right, bottom-left, bottom-right (64 words
 * each). Confirmed two ways against the real vendored source (not assumed):
 *   1. vdp2_scrn_pnd_set()'s aux-mode bit-packing
 *      (third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_cell.c:320-356)
 *      supplements the pattern-name table's character number with two
 *      implicit low bits that select one of the four 8x8 sub-cells --
 *      those bits only make sense if the four sub-cells are separately
 *      addressed characters, not one contiguous 16-wide image.
 *   2. Yaul's own texture converter's TILE_16x16 case reads exactly those
 *      four 8x8 quadrants, in that order, into one contiguous 256-byte
 *      buffer (third_party/libyaul/tools/satconv/tile.c:177-208: "top
 *      left" / "top right" / "bottom left" / "bottom right" comments on
 *      the four tile_read() calls at offsets 0, 64, 128, 192).
 * A naive linear copy of a row-major 16-wide source raster into that
 * layout would interleave rows from different quadrants and produce a
 * scrambled glyph on real hardware and in cycle-accurate emulation alike.
 *
 * src_pixels is read as a src_stride-wide raster; only the src_width x
 * src_height texels starting at source (0,0) are used. This lets one
 * helper serve full 16x16 glyphs (stride == width == height == 16), native
 * 8x8 glyphs placed in the top-left cell (stride == width == height == 8),
 * and a 16x16 top-left crop taken from a wider 32x32 source (stride == 32,
 * width == height == 16), the last of which the power-meter glyphs need
 * because their real source art is 32x32 (see the call sites below). Any
 * destination texel not covered by the source is written as solid
 * transparent (0x0000, matching this project's established "MSB clear ==
 * transparent" RGB1555 convention -- also used by
 * tools/saturn/extract_mario_textures.py's saturn_rgb1555()) rather than
 * left at whatever the VRAM word previously held: Saturn VRAM contents are
 * not guaranteed to be zero at power-on, and this bank is not otherwise
 * cleared before this function runs. */
static void
hud_atlas_upload_pattern(sm64_saturn_hud_glyph_t glyph, const uint16_t *src_pixels,
                         uint32_t src_stride, uint32_t src_width, uint32_t src_height)
{
    volatile uint16_t *const dest = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | (HUD_CPD_BASE + (uint32_t)glyph * HUD_CHAR_BYTES));

    for (uint32_t quadrant_y = 0U; quadrant_y < 2U; quadrant_y++) {
        for (uint32_t quadrant_x = 0U; quadrant_x < 2U; quadrant_x++) {
            const uint32_t cell_word_base = (quadrant_y * 2U + quadrant_x) * 64U;
            for (uint32_t local_y = 0U; local_y < 8U; local_y++) {
                const uint32_t src_y = quadrant_y * 8U + local_y;
                for (uint32_t local_x = 0U; local_x < 8U; local_x++) {
                    const uint32_t src_x = quadrant_x * 8U + local_x;
                    const uint32_t dest_index = cell_word_base + local_y * 8U + local_x;
                    dest[dest_index] = (src_x < src_width && src_y < src_height)
                        ? src_pixels[src_y * src_stride + src_x]
                        : 0x0000U;
                }
            }
        }
    }
}

void
sm64_saturn_hud_atlas_init(void)
{
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_0, sm64_saturn_hud_digit_0,
                             SM64_SATURN_HUD_DIGIT_0_WIDTH, SM64_SATURN_HUD_DIGIT_0_WIDTH, SM64_SATURN_HUD_DIGIT_0_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_1, sm64_saturn_hud_digit_1,
                             SM64_SATURN_HUD_DIGIT_1_WIDTH, SM64_SATURN_HUD_DIGIT_1_WIDTH, SM64_SATURN_HUD_DIGIT_1_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_2, sm64_saturn_hud_digit_2,
                             SM64_SATURN_HUD_DIGIT_2_WIDTH, SM64_SATURN_HUD_DIGIT_2_WIDTH, SM64_SATURN_HUD_DIGIT_2_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_3, sm64_saturn_hud_digit_3,
                             SM64_SATURN_HUD_DIGIT_3_WIDTH, SM64_SATURN_HUD_DIGIT_3_WIDTH, SM64_SATURN_HUD_DIGIT_3_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_4, sm64_saturn_hud_digit_4,
                             SM64_SATURN_HUD_DIGIT_4_WIDTH, SM64_SATURN_HUD_DIGIT_4_WIDTH, SM64_SATURN_HUD_DIGIT_4_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_5, sm64_saturn_hud_digit_5,
                             SM64_SATURN_HUD_DIGIT_5_WIDTH, SM64_SATURN_HUD_DIGIT_5_WIDTH, SM64_SATURN_HUD_DIGIT_5_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_6, sm64_saturn_hud_digit_6,
                             SM64_SATURN_HUD_DIGIT_6_WIDTH, SM64_SATURN_HUD_DIGIT_6_WIDTH, SM64_SATURN_HUD_DIGIT_6_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_7, sm64_saturn_hud_digit_7,
                             SM64_SATURN_HUD_DIGIT_7_WIDTH, SM64_SATURN_HUD_DIGIT_7_WIDTH, SM64_SATURN_HUD_DIGIT_7_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_8, sm64_saturn_hud_digit_8,
                             SM64_SATURN_HUD_DIGIT_8_WIDTH, SM64_SATURN_HUD_DIGIT_8_WIDTH, SM64_SATURN_HUD_DIGIT_8_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DIGIT_9, sm64_saturn_hud_digit_9,
                             SM64_SATURN_HUD_DIGIT_9_WIDTH, SM64_SATURN_HUD_DIGIT_9_WIDTH, SM64_SATURN_HUD_DIGIT_9_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_MULTIPLY, sm64_saturn_hud_glyph_multiply,
                             SM64_SATURN_HUD_GLYPH_MULTIPLY_WIDTH, SM64_SATURN_HUD_GLYPH_MULTIPLY_WIDTH, SM64_SATURN_HUD_GLYPH_MULTIPLY_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_COIN, sm64_saturn_hud_glyph_coin,
                             SM64_SATURN_HUD_GLYPH_COIN_WIDTH, SM64_SATURN_HUD_GLYPH_COIN_WIDTH, SM64_SATURN_HUD_GLYPH_COIN_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_MARIO_HEAD, sm64_saturn_hud_glyph_mario_head,
                             SM64_SATURN_HUD_GLYPH_MARIO_HEAD_WIDTH, SM64_SATURN_HUD_GLYPH_MARIO_HEAD_WIDTH, SM64_SATURN_HUD_GLYPH_MARIO_HEAD_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_STAR, sm64_saturn_hud_glyph_star,
                             SM64_SATURN_HUD_GLYPH_STAR_WIDTH, SM64_SATURN_HUD_GLYPH_STAR_WIDTH, SM64_SATURN_HUD_GLYPH_STAR_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_APOSTROPHE, sm64_saturn_hud_glyph_apostrophe,
                             SM64_SATURN_HUD_GLYPH_APOSTROPHE_WIDTH, SM64_SATURN_HUD_GLYPH_APOSTROPHE_WIDTH, SM64_SATURN_HUD_GLYPH_APOSTROPHE_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE, sm64_saturn_hud_glyph_double_quote,
                             SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE_WIDTH, SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE_WIDTH, SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_CAMERA, sm64_saturn_hud_cam_camera,
                             SM64_SATURN_HUD_CAM_CAMERA_WIDTH, SM64_SATURN_HUD_CAM_CAMERA_WIDTH, SM64_SATURN_HUD_CAM_CAMERA_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD, sm64_saturn_hud_cam_mario_head,
                             SM64_SATURN_HUD_CAM_MARIO_HEAD_WIDTH, SM64_SATURN_HUD_CAM_MARIO_HEAD_WIDTH, SM64_SATURN_HUD_CAM_MARIO_HEAD_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD, sm64_saturn_hud_cam_lakitu_head,
                             SM64_SATURN_HUD_CAM_LAKITU_HEAD_WIDTH, SM64_SATURN_HUD_CAM_LAKITU_HEAD_WIDTH, SM64_SATURN_HUD_CAM_LAKITU_HEAD_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_FIXED, sm64_saturn_hud_cam_fixed,
                             SM64_SATURN_HUD_CAM_FIXED_WIDTH, SM64_SATURN_HUD_CAM_FIXED_WIDTH, SM64_SATURN_HUD_CAM_FIXED_HEIGHT);
    /* Arrows are native 8x8 source art (SM64_SATURN_HUD_CAM_ARROW_*_WIDTH/
     * HEIGHT are both 8U in the generated header). hud_atlas_upload_pattern
     * places the real pixels in the top-left 8x8 cell (matching hud.c's own
     * render_hud_small_tex_lut distinction between 16x16 and 8x8 source
     * glyph sizes, which the layout builder in Task 5 will place with an
     * 8px-aware offset) and writes solid transparent into the other three
     * 8x8 cells of the pattern, rather than leaving them at whatever the
     * VRAM bank previously held. */
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP, sm64_saturn_hud_cam_arrow_up,
                             SM64_SATURN_HUD_CAM_ARROW_UP_WIDTH, SM64_SATURN_HUD_CAM_ARROW_UP_WIDTH, SM64_SATURN_HUD_CAM_ARROW_UP_HEIGHT);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN, sm64_saturn_hud_cam_arrow_down,
                             SM64_SATURN_HUD_CAM_ARROW_DOWN_WIDTH, SM64_SATURN_HUD_CAM_ARROW_DOWN_WIDTH, SM64_SATURN_HUD_CAM_ARROW_DOWN_HEIGHT);
    /* Power meter source art is real 32x32 pixels (SM64_SATURN_HUD_POWER_
     * METER_*_WIDTH/HEIGHT are both 32U in the generated header -- each
     * array holds 1024 uint16_t texels, not 256), so this renders one
     * representative 16x16 cell per wedge count by cropping the top-left
     * 16x16 region of that 32x32 source (src_stride=32 preserves the real
     * source row length so the crop reads the correct texels instead of
     * the first 8 full-width source rows; src_width=src_height=16 bounds
     * the crop and drives the same quadrant reordering every other glyph
     * gets). Only wedges 1-8 exist as source assets -- hud.c's
     * render_hud_power_meter() never renders the meter at 0 wedges
     * (POWER_METER_HIDDEN returns early, hud.c:236-238), and
     * power_meter_health_segments_lut itself is indexed
     * [numHealthWedges - 1] with no 0-wedge slot -- so there is no
     * "power_meter_0" source texture to extract or upload. */
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_1, sm64_saturn_hud_power_meter_1,
                             SM64_SATURN_HUD_POWER_METER_1_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_2, sm64_saturn_hud_power_meter_2,
                             SM64_SATURN_HUD_POWER_METER_2_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_3, sm64_saturn_hud_power_meter_3,
                             SM64_SATURN_HUD_POWER_METER_3_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_4, sm64_saturn_hud_power_meter_4,
                             SM64_SATURN_HUD_POWER_METER_4_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_5, sm64_saturn_hud_power_meter_5,
                             SM64_SATURN_HUD_POWER_METER_5_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_6, sm64_saturn_hud_power_meter_6,
                             SM64_SATURN_HUD_POWER_METER_6_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_7, sm64_saturn_hud_power_meter_7,
                             SM64_SATURN_HUD_POWER_METER_7_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);
    hud_atlas_upload_pattern(SM64_SATURN_HUD_GLYPH_POWER_METER_8, sm64_saturn_hud_power_meter_8,
                             SM64_SATURN_HUD_POWER_METER_8_WIDTH, HUD_CHAR_DIM, HUD_CHAR_DIM);

    /* Cannon reticle and blank are procedural, not extracted -- solid gray
     * (matching ingame_menu.c's gDPSetEnvColor(50,50,50,180)) and solid
     * transparent (alpha bit 0 clear) respectively. Both fill every word of
     * their 16x16 pattern with one constant value, so unlike the glyph
     * uploads above, quadrant order doesn't matter here: a uniform fill
     * looks identical no matter how the 256 words are grouped into cells. */
    {
        volatile uint16_t *const reticle = (volatile uint16_t *)
            (CPU_CACHE_THROUGH | (HUD_CPD_BASE +
             (uint32_t)SM64_SATURN_HUD_GLYPH_CANNON_RETICLE * HUD_CHAR_BYTES));
        volatile uint16_t *const blank = (volatile uint16_t *)
            (CPU_CACHE_THROUGH | (HUD_CPD_BASE +
             (uint32_t)SM64_SATURN_HUD_GLYPH_BLANK * HUD_CHAR_BYTES));
        for (uint32_t index = 0U; index < 256U; index++) {
            reticle[index] = 0x8000U | (6U << 10) | (6U << 5) | 6U; /* opaque gray */
            blank[index] = 0x0000U; /* transparent: alpha bit clear */
        }
    }
    cpu_cache_purge();

    const vdp2_scrn_cell_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG0,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .char_size = VDP2_SCRN_CHAR_SIZE_2X2,
        .pnd_size = 1U,
        .aux_mode = VDP2_SCRN_AUX_MODE_1,
        .plane_size = VDP2_SCRN_PLANE_SIZE_1X1,
        .cpd_base = HUD_CPD_BASE,
        .palette_base = 0U,
    };
    const vdp2_scrn_normal_map_t map = {
        .plane_a = HUD_PND_BASE,
        .plane_b = HUD_PND_BASE,
        .plane_c = HUD_PND_BASE,
        .plane_d = HUD_PND_BASE,
    };
    vdp2_scrn_cell_format_set(&format, &map);

    for (uint16_t row = 0U; row < HUD_TILE_ROWS; row++) {
        for (uint16_t col = 0U; col < HUD_TILE_COLS; col++)
            sm64_saturn_hud_atlas_write_cell((uint8_t)col, (uint8_t)row,
                                              SM64_SATURN_HUD_GLYPH_BLANK);
    }
}

void
sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row,
                                 sm64_saturn_hud_glyph_t glyph)
{
    if (col >= HUD_TILE_COLS || row >= HUD_TILE_ROWS ||
        glyph >= SM64_SATURN_HUD_GLYPH_COUNT)
        return;
    const uint32_t cell_index = (uint32_t)row * HUD_PAGE_STRIDE_COLS + col;
    volatile uint16_t *const pnd = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | (HUD_PND_BASE + cell_index * 2U));
    const uint32_t cpd_addr = HUD_CPD_BASE + (uint32_t)glyph * HUD_CHAR_BYTES;
    /* CONFIG_3, not CONFIG_1: the 1-word PND encoding is a function of this
     * screen's char_size + aux_mode (set in the cell format above:
     * CHAR_SIZE_2X2 + AUX_MODE_1), not a free choice. With a 2x2 character
     * size the hardware addresses characters in 2x2-cell (16x16-texel) units,
     * so the character number's low 2 bits (the 8x8 sub-cell selectors) drop
     * out of the pattern-name word and the PND field holds char# bits 13-2:
     *   VDP2_SCRN_PND_CONFIG_3 packs (CP_NUM >> 2) & 0x0FFF, where
     *   CP_NUM = cpd_addr >> 5 (scrn_macros.h:135,159-161), i.e.
     *   (cpd_addr >> 7) & 0x0FFF;
     * libyaul's supplement writer mirrors exactly this split for
     * aux-mode-1/2x2 ("Character number in pattern name table: bits 13~2",
     * vdp2_scrn_cell.c:347-351), with the PNC supplement providing bits
     * 14/1/0. CONFIG_1 is the 1x1-character packing (char# bits 11-0,
     * CP_NUM & 0x0FFF unshifted): under 2x2 decode the hardware re-scales
     * that value by 4, which resolved every glyph into VRAM bank A0 (the
     * NBG1 sky bitmap) instead of the atlas at HUD_CPD_BASE in bank B0 --
     * the target-proven Task 9 blank-HUD defect (PND injection of the
     * CONFIG_3 word rendered the glyph pixel-exactly). */
    *pnd = (uint16_t)VDP2_SCRN_PND_CONFIG_3(0, cpd_addr, 0);
}
