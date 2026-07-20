/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>

#include "game/game_init.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_source_runtime.h"
#include "source_cart.h"

/* This is an internal-WRAM bootstrap arena, deliberately not the 4 MiB cart.
 * E3 packages use the cart for immutable level banks; source allocator demand
 * remains measurable here until the multi-arena policy is ready. */
#define SOURCEBOOT_MAIN_POOL_BYTES (0x00030000UL)
static uint8_t sourceboot_main_pool[SOURCEBOOT_MAIN_POOL_BYTES] __aligned(16);
static sm64_saturn_fast3d_frontend_t sourceboot_fast3d;

void user_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                              VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 0, 0, 0));
    vdp2_tvmd_display_set();
}

int main(void) {
    const sm64_saturn_source_cart_status_t cart_status =
        sm64_saturn_source_cart_load();
    if (cart_status != SM64_SATURN_SOURCE_CART_OK) {
        sm64_saturn_source_cart_report_failure(cart_status);
        for (;;) {}
    }

    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    dbgio_puts("\x1B[H\x1B[2JSM64 SATURN SOURCEBOOT E2\n"
               "Direct original Bob script\n"
               "SOURCE.DAT -> 4 MiB RAM cart\n"
               "Source loop -> Fast3D task intake\n");
    dbgio_flush();

    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);
    sm64_saturn_source_runtime_configure(sm64_saturn_fast3d_frontend_submit,
                                         &sourceboot_fast3d);

    main_pool_init(sourceboot_main_pool,
                   sourceboot_main_pool + sizeof(sourceboot_main_pool));
    gEffectsMemoryPool = mem_pool_init(0x4000U, MEMORY_POOL_LEFT);
    if (gEffectsMemoryPool == NULL) {
        dbgio_puts("sourceboot: effects pool allocation failed\n");
        dbgio_flush();
        for (;;) {}
    }

    /* These are the unmodified source-port calls used by src/pc/pc_main.c:
     * bootstrap once, then advance exactly one source frame per iteration. */
    thread5_game_loop(NULL);
    for (;;) {
        game_loop_one_iteration();
    }
}
