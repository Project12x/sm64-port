#include <yaul.h>

void
user_init(void)
{
        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
            VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);

        vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            RGB1555(1, 0, 0, 4));

        vdp2_tvmd_display_set();
}

int
main(void)
{
        dbgio_init();
        dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
        dbgio_dev_font_load();

        dbgio_puts("\x1B[H\x1B[2J"
                   "SM64 SATURN PORT\n"
                   "\n"
                   "libyaul 0.3.1 / 6012f79\n"
                   "Phase 0: hello-disc bring-up\n");

        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();

        for (;;) {
        }

        return 0;
}
