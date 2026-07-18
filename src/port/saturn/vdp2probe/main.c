#include <yaul.h>
#include <cpu/cache.h>

#define BITMAP_BASE VDP2_VRAM_ADDR(0, 0x00000)
#define BITMAP_WIDTH 512U
#define BITMAP_HEIGHT 256U

void
user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
      VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
      RGB1555(1, 31, 0, 0));
    vdp2_tvmd_display_set();
}

int
main(void)
{
    /* Use the SH-2 cache-through alias deliberately. This is the smallest
     * possible test of an NBG1 RGB555 bitmap: no VDP1 framebuffer, no dbgio,
     * and no DMA queue that could obscure a direct VDP2 VRAM write. */
    volatile rgb1555_t * const pixels = (volatile rgb1555_t *)(
      CPU_CACHE_THROUGH | BITMAP_BASE);
    for (uint16_t y = 0; y < BITMAP_HEIGHT; y++) {
        for (uint16_t x = 0; x < BITMAP_WIDTH; x++) {
            const uint8_t stripe = (uint8_t)((x >> 5) & 7U);
            pixels[(uint32_t)y * BITMAP_WIDTH + x] = RGB1555(1,
              (uint8_t)(4U + stripe * 3U), (uint8_t)(y >> 4),
              (uint8_t)(31U - stripe * 2U));
        }
    }

    const vdp2_scrn_bitmap_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG1,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = BITMAP_BASE,
    };
    /* Close-port of the VRAM-cycle allocation pattern in Yaul's MIT-licensed
     * vdp2-normal-bitmap example (develop; inspected 2026-07-18). A 512x256
     * RGB555 bitmap occupies the first two 128 KiB banks, and VDP2 must be
     * granted character-pattern fetch slots for NBG1 in both. */
    const vdp2_vram_cycp_t cycles = {
        .pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
    };
    vdp2_vram_cycp_set(&cycles);
    vdp2_scrn_bitmap_format_set(&format);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 7);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG1);
    /* VDP2's shadow registers are committed from the normal post-boot main
     * path, as in Yaul's hello target; user_init only selects the TV mode. */
    vdp2_sync();
    vdp2_sync_wait();
    for (;;) {}
}
