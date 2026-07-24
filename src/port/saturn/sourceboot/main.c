/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>

#include "game/game_init.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_gouraud_bank.h"
#include "saturn_source_runtime.h"
#include "saturn_vdp1_backend.h"
#include "source_cart.h"
#include "../gpl/slavedriver_dma_queue.h" /* gpl/ is a sibling of sourceboot/
                                           * under src/port/saturn/; matches
                                           * hwtest's existing include style
                                           * since no -I path exposes gpl/
                                           * by bare name (see the Makefile's
                                           * SH_CFLAGS -I list). */

/* SM64's bootstrap main pool, LWRAM-resident. History of this placement:
 *
 * - Originally 0x30000 in HWRAM .bss. That filled HWRAM to within ~188
 *   bytes of the top, which was boot-fatal: libyaul's __mm_init()
 *   (kernel/mm/internal.c) creates the user TLSF heap over
 *   [__end, 0x06100000), and TLSF's multi-KiB control block wrote past
 *   the physical end of HWRAM, which MIRRORS back to 0x06000000 --
 *   low-memory corruption before main(), SH-2 exception cascade.
 * - Emergency-shrunk to 0x28000 to give TLSF room. That was worse in a
 *   quieter way: Bob-omb Battlefield's collision load needs ~166 KiB of
 *   pool (surface pool 2300*48 + node pool 7000*8) plus the effects
 *   pool; alloc_surface_pools()'s surface allocation then failed to
 *   NULL (unchecked in stock SM64), surfaces were silently written into
 *   ROM address space, the garbage read-back made every surface span
 *   ~29 partition cells, and the unchecked node allocator overran its
 *   56 KiB region straight out of the pool's end through adjacent .bss
 *   (diagnosed live via the cart-enabled headless rig: sSurfacePool=0,
 *   gSurfaceNodesAllocated=13,872 of 7,000).
 * - Now: 0x60000 (384 KiB) in LWRAM, where ~1 MiB sits idle next to
 *   the 16 KiB VDP1 staging array. The old 0x30000 was itself too small
 *   for Bob's full load: with level geo/display data allocated first,
 *   the 110 KiB surface pool still failed (measured live: freeSpace
 *   77,520 at the failure point). CPU access to LWRAM is unrestricted;
 *   the one hardware rule is that SCU DMA must never touch it (see
 *   docs/saturn/SGL_REFERENCE_NOTES.md), so nothing may SCU-DMA pool
 *   contents -- all current consumers (collision, level/geo data, the
 *   Fast3D frontend's CPU reads) are CPU-only. LWRAM is the slower RAM
 *   bank; if profiling later shows hot game state suffering, move the
 *   hot subset back to HWRAM headroom, which this placement frees up.
 * - .lwram_bss is NOLOAD (never crt0-zeroed). main_pool_init() writes
 *   its own block headers and SM64 treats pool contents as
 *   alloc-then-write, matching N64 boot RAM semantics. */
#define SOURCEBOOT_MAIN_POOL_BYTES (0x00060000UL)
static uint8_t sourceboot_main_pool[SOURCEBOOT_MAIN_POOL_BYTES]
    __attribute__((section(".lwram_bss"))) __aligned(16);
static sm64_saturn_fast3d_frontend_t sourceboot_fast3d;

#define SOURCEBOOT_VDP1_COMMAND_CAPACITY 2048U

/* LWRAM-resident command staging -- see sourceboot-cart.x's new lwram
 * MEMORY region/.lwram_cmdts section. Zeroed explicitly by
 * sm64_saturn_vdp1_backend_init_with_storage below, since this section
 * is not .bss and crt0 never visits it.
 *
 * Capacity raised 512 -> 2048 (2026-07-22) to track
 * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES's 192 -> 1536 increase 1:1
 * (saturn_fast3d_vdp1_emit.c emits exactly one vdp1_cmdt_t per resolved
 * triangle). 2048 * 32 bytes = 64 KiB, trivial against the ~1 MiB free
 * in the lwram region (sourceboot-cart.x) -- no linker script change
 * needed. */
static vdp1_cmdt_t sourceboot_vdp1_cmdts[SOURCEBOOT_VDP1_COMMAND_CAPACITY]
    __attribute__((section(".lwram_cmdts")));
static sm64_saturn_vdp1_backend_t sourceboot_vdp1_backend;

/* HWRAM (.bss) deliberately: SCU DMA from LWRAM is the documented
 * lockup class the VDP1 backend above already works around (see its
 * header comment). 1536 * 8 = 12,288 bytes against the ~191 KiB
 * measured HWRAM margin (this file's SOURCEBOOT_MAIN_POOL_BYTES
 * comment). One table per resolved triangle, rebuilt every frame
 * (bank_begin) and uploaded used-prefix-only after emission -- see
 * saturn_fast3d_vdp1_emit.c. */
static sm64_saturn_gouraud_table_t
    sourceboot_gouraud_staging[SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
static sm64_saturn_gouraud_bank_t sourceboot_gouraud_bank;

/* Per-frame SMPC INTBACK request, on the same VBLANK-OUT cadence the two
 * proven sibling targets use (marioturntable/main.c's vblank_out_handler;
 * castleviewer registers the identical handler). Without a fresh INTBACK
 * issued every frame, the SMPC never collects another peripheral report
 * after boot: controller_saturn.c's read path
 * (smpc_peripheral_process() + smpc_peripheral_digital_port()) then sees
 * a permanently empty report, returns CONT_NO_RESPONSE, and gControllers[0]
 * stays neutral forever -- the game is unplayable even once it reaches
 * free roam. This target simply never had the service wired up: it is the
 * only Saturn target that consumes controller state through the real game
 * loop, and the boot worked without it because the BIOS sequence uses the
 * BIOS's own pad handling. */
static void sourceboot_vblank_out_handler(void *work __unused) {
    smpc_peripheral_intback_issue();
}

void user_init(void) {
    /* First, matching both siblings' user_init order (castleviewer
     * main.c:1186, marioturntable main.c:247). */
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                              VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 0, 0, 0));
    /* VDP1's output is a VDP2-composited layer: sprite-screen priority 0
     * means "never displayed" (the classic footgun recorded in
     * docs/saturn/SGL_REFERENCE_NOTES.md). Without this, the whole
     * Fast3D-to-VDP1 pipeline draws into an invisible layer -- diagnosed
     * live when the first 18 resolved triangles produced a black frame.
     * Mirrors castleviewer's proven setup (all 8 groups at 7). */
    for (uint8_t priority = 0; priority < 8; priority++) {
        vdp2_sprite_priority_set(priority, 7);
    }
    vdp2_tvmd_display_set();
    /* Register the per-frame INTBACK cadence and prime the first
     * collection, mirroring castleviewer verbatim (its comment: a target
     * that starts polling before the first VBLANK can otherwise retain an
     * all-zero, disconnected OSContPad sample). */
    vdp_sync_vblank_out_set(sourceboot_vblank_out_handler, NULL);
    smpc_peripheral_intback_issue();
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
    /* Layer visibility, after dbgio's own VDP2 setup so nothing below
     * re-clobbers it -- the ordering castleviewer/marioturntable ship
     * with. NBG3 carries dbgio's text; the sprite groups carry VDP1's
     * composited output (priority 0 = invisible; see
     * docs/saturn/SGL_REFERENCE_NOTES.md). Without these, both the boot
     * banner and every rendered triangle land in invisible layers. */
    for (uint8_t priority = 0; priority < 8; priority++) {
        vdp2_sprite_priority_set(priority, 7);
    }
    vdp2_scrn_priority_set(VDP2_SCRN_NBG3, 7);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG3);
    dbgio_puts("\x1B[H\x1B[2JSM64 SATURN SOURCEBOOT E2\n"
               "Direct original Bob script\n"
               "SOURCE.DAT -> 4 MiB RAM cart\n"
               "Source loop -> Fast3D task intake\n");
    dbgio_flush();
    /* Commit everything VDP2-side queued so far -- the layer priorities
     * above, the back color, and dbgio's text DMA. libyaul buffers VDP2
     * state in shadow registers that reach hardware only when
     * vdp2_sync() arms the vblank commit (the same
     * shadow-then-commit-at-vblank model Sega's own SGL documents; see
     * SGL_REFERENCE_NOTES.md). Without this, every VDP2 write since
     * boot -- including this banner -- stays invisible; the proven
     * hello/hwtest targets all pair dbgio_flush() with exactly this. */
    vdp2_sync();
    vdp2_sync_wait();

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

    {
        vdp1_vram_partitions_t partitions;
        uintptr_t cmd_end = (uintptr_t)VDP1_VRAM(0) +
            (uintptr_t)SOURCEBOOT_VDP1_COMMAND_CAPACITY *
                sizeof(vdp1_cmdt_t);
        uint16_t capacity = 0;

        /* Stock Yaul default (__vdp_init(), run by crt0 before main())
         * only reserves VDP1_VRAM_DEFAULT_GOURAUD_COUNT (1024) tables =
         * 8192 bytes -- confirmed against the vendored libyaul sources
         * (work/upstream/libyaul/libyaul/scu/bus/b/vdp/vdp_init.c:50-53
         * and vdp1_vram.c:22-78), not just the installed headers. Real
         * captured Bob-omb Battlefield free-roam frames run 1,365-1,431
         * triangles (SOURCEBOOT_VDP1_COMMAND_CAPACITY's comment above),
         * so the stock 1024-table cap would push the ORDINARY case into
         * the flat-fallback path, not a rare edge case. Re-partition
         * explicitly so gouraud covers the full MAX_RESOLVED_TRIANGLES
         * (1536) worst case -- see this task's commit message for the
         * full byte-budget arithmetic against VDP1_VRAM_SIZE.
         *
         * texture_size and clut_count go to 0: this backend does not
         * use Yaul's partition-aware texture or CLUT allocation
         * anywhere in this target (verified: partitions.texture_base/
         * clut_base have no consumer in sourceboot's own sources --
         * only the separate hwtest/castleviewer/marioturntable binaries
         * reference those fields, each with its own independent
         * partition setup in its own main()). Freeing that space is
         * what makes room for the larger gouraud partition.
         *
         * cmdt_count stays at SOURCEBOOT_VDP1_COMMAND_CAPACITY so
         * Yaul's own bookkeeping matches the size of the command region
         * this backend actually writes (VDP1_VRAM(0) CPU-copy in
         * sm64_saturn_vdp1_backend_upload, bypassing Yaul's
         * partition-aware cmdt allocator entirely -- see
         * saturn_vdp1_backend.h), even though nothing on this path
         * reads partitions.cmdt_base. */
        vdp1_vram_partitions_set(SOURCEBOOT_VDP1_COMMAND_CAPACITY, 0U,
                                 SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES,
                                 0U);

        saturn_dma_queue_init();
        vdp1_vram_partitions_get(&partitions);
        /* The backend CPU-copies its command list to VDP1_VRAM(0)
         * without consulting Yaul's partition layout -- verify the
         * gouraud partition clears the command region before trusting
         * it. Overlap => capacity 0 => every triangle takes the
         * counted flat fallback (degradation contract), no crash. */
        if ((uintptr_t)partitions.gouraud_base >= cmd_end &&
            partitions.gouraud_size >=
                sizeof(sm64_saturn_gouraud_table_t)) {
            uint32_t fit = partitions.gouraud_size /
                sizeof(sm64_saturn_gouraud_table_t);
            capacity = (uint16_t)(fit >
                SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES ?
                SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES : fit);
        } else {
            dbgio_puts("sourceboot: gouraud partition unusable\n");
        }
        (void)sm64_saturn_gouraud_bank_init(&sourceboot_gouraud_bank,
            sourceboot_gouraud_staging, capacity,
            (uintptr_t)partitions.gouraud_base);
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
                                     &sourceboot_vdp1_backend,
                                     &sourceboot_gouraud_bank);

        /* VDP1 runs in Yaul's default "auto" (1-cycle) interval mode here
         * (vdp1_sync_interval_set(0), set unconditionally by libyaul's
         * __vdp_init() before main() runs; this target never changes it) --
         * i.e. single-buffered: draw and display share the same VRAM command
         * table. sm64_saturn_fast3d_vdp1_emit() -> backend_upload() copies a
         * fresh table into that SAME address every frame (this target's
         * SM64_SATURN_VDP1_LWRAM_STAGING CPU-copy path in
         * saturn_vdp1_backend.h -- SCU DMA cannot read this target's
         * LWRAM staging array; see the dispatch comment there).
         * Nothing guards that upload against
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
         * vdp1_sync_render() does not block at all in this configuration --
         * backend_upload()'s LWRAM path completes its copy synchronously and
         * leaves LIST_XFERRED already set (vdp1_sync_force_put) before it
         * returns; vdp1_sync() just sets flags. The ISR-driven advance to "list
         * committed" (next VBLANK-IN) and back to idle (the VBLANK-OUT that
         * follows) then completes for free during the *existing* raw-poll
         * wait inside the NEXT game_loop_one_iteration() call -- the VBLANK
         * ISRs fire on the real hardware edges regardless of what the
         * foreground loop is polling, so by the time that poll returns, the
         * state machine has already cycled back to idle. If a frame ever
         * runs long and the state machine hasn't caught up in time,
         * backend_upload()'s own pre-copy vdp1_sync_wait() guard
         * (saturn_vdp1_backend.h) still blocks as a self-correcting
         * fallback -- so this
         * is safe even under a dropped frame, it just only costs an explicit
         * wait when one is actually needed instead of unconditionally every
         * frame.
         *
         * vdp2_sync() IS armed each frame (a flags-only call, no blocking
         * -- the vblank-in ISR performs the actual commit, so this adds
         * no second wait): an earlier revision omitted it on the
         * reasoning that "nothing queues VDP2 writes after user_init",
         * which was wrong -- dbgio's async device queues VDP2 VRAM
         * transfers whenever game code prints, and any future VDP2 state
         * change (fades, letterboxing) needs the commit armed. The
         * blocking vdp2_sync_wait() stays out of the loop per the pacing
         * analysis above. */
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
        /* BRING-UP OVERRIDE of the pacing analysis above: block on the
         * full sync completion each frame, exactly like the
         * proven-visible hwtest/castleviewer choreography. The measured
         * game rate is currently ~1 fps (soft-float dominated), so the
         * extra vblank pair this costs is noise today; remove this (and
         * re-verify against the non-blocking analysis above) when the
         * performance work starts. */
        vdp2_sync_wait();
        vdp1_sync_wait();
    }
}
