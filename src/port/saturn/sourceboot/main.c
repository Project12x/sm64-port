/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>

#include "game/game_init.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_source_runtime.h"
#include "saturn_vdp1_backend.h"
#include "source_cart.h"

/* This is an internal-WRAM bootstrap arena, deliberately not the 4 MiB cart.
 * E3 packages use the cart for immutable level banks; source allocator demand
 * remains measurable here until the multi-arena policy is ready. */
#define SOURCEBOOT_MAIN_POOL_BYTES (0x00030000UL)
static uint8_t sourceboot_main_pool[SOURCEBOOT_MAIN_POOL_BYTES] __aligned(16);
static sm64_saturn_fast3d_frontend_t sourceboot_fast3d;

#define SOURCEBOOT_VDP1_COMMAND_CAPACITY 512U

/* LWRAM-resident command staging -- see sourceboot-cart.x's new lwram
 * MEMORY region/.lwram_cmdts section. Zeroed explicitly by
 * sm64_saturn_vdp1_backend_init_with_storage below, since this section
 * is not .bss and crt0 never visits it. */
static vdp1_cmdt_t sourceboot_vdp1_cmdts[SOURCEBOOT_VDP1_COMMAND_CAPACITY]
    __attribute__((section(".lwram_cmdts")));
static sm64_saturn_vdp1_backend_t sourceboot_vdp1_backend;

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

    {
        const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init_with_storage(
                &sourceboot_vdp1_backend, sourceboot_vdp1_cmdts,
                SOURCEBOOT_VDP1_COMMAND_CAPACITY, clip, local)) {
            dbgio_puts("sourceboot: VDP1 backend init failed\n");
            dbgio_flush();
            for (;;) {}
        }
    }

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
        sm64_saturn_fast3d_vdp1_emit(&sourceboot_fast3d,
                                     &sourceboot_vdp1_backend);

        /* VDP1 runs in Yaul's default "auto" (1-cycle) interval mode here
         * (vdp1_sync_interval_set(0), set unconditionally by libyaul's
         * __vdp_init() before main() runs; this target never changes it) --
         * i.e. single-buffered: draw and display share the same VRAM command
         * table. sm64_saturn_fast3d_vdp1_emit() -> backend_upload() DMAs a
         * fresh table into that SAME address every frame
         * (vdp1_sync_cmdt_list_put(..., 0)). Nothing guards that DMA against
         * landing while VDP1 is still plotting from the PREVIOUS table
         * unless libyaul's vdp_sync flag state machine (vdp_sync.c) is armed
         * and given a chance to advance through one VBLANK-IN (presumed
         * "plot committed") and the following VBLANK-OUT (safe to swap).
         *
         * castleviewer/marioturntable arm *and* fully block on that state
         * machine every frame:
         *   vdp1_sync_render(); vdp1_sync(); vdp2_sync();
         *   vdp2_sync_wait(); vdp1_sync_wait();
         * That block is itself a second, independent VBLANK-IN+OUT wait. For
         * those targets it's harmless -- it's their ONLY per-frame wait (or
         * their loop is so CPU-bound the extra wait is noise). It is NOT
         * harmless here: game_loop_one_iteration() -> display_and_vsync() ->
         * sm64_saturn_source_runtime_wait_vblank() (saturn_source_runtime.c)
         * already raw-polls VDP2 TVSTAT for one VBLANK-IN+OUT pair per
         * iteration to pace the stock game loop. The raw TVMD poll and the
         * vdp_sync module's ISR-driven flags are two independent
         * observers of the same physical VBLANK edges, sharing no state;
         * appending the full castleviewer dance here would make the loop
         * wait through a SECOND, separate VBLANK-IN+OUT pair every
         * iteration -- silently halving the effective game loop rate.
         *
         * Fix: arm the state machine but do not block on it here.
         * vdp1_sync_render() only blocks for the command-table DMA this
         * same emit() call just started (microseconds, not a vblank);
         * vdp1_sync() just sets flags. The ISR-driven advance to "list
         * committed" (next VBLANK-IN) and back to idle (the VBLANK-OUT that
         * follows) then completes for free during the *existing* raw-poll
         * wait inside the NEXT game_loop_one_iteration() call -- the VBLANK
         * ISRs fire on the real hardware edges regardless of what the
         * foreground loop is polling, so by the time that poll returns, the
         * state machine has already cycled back to idle. If a frame ever
         * runs long and the state machine hasn't caught up in time,
         * vdp1_sync_cmdt_list_put()'s own internal wait (_vdp1_sync_put(),
         * vdp_sync.c) still blocks as a self-correcting fallback -- so this
         * is safe even under a dropped frame, it just only costs an explicit
         * wait when one is actually needed instead of unconditionally every
         * frame.
         *
         * vdp2_sync()/vdp2_sync_wait() are omitted entirely: they commit
         * queued VDP2 register writes, and nothing reachable from this loop
         * (stock game code, the Fast3D frontend, or the VDP1 backend)
         * touches a VDP2 register after user_init()'s one-time setup above
         * -- there is nothing queued to commit. */
        vdp1_sync_render();
        vdp1_sync();
    }
}
