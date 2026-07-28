/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>

#include "game/camera.h"
#include "game/game_init.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_actor_bridge.h"
#include "saturn_demo_render.h"
#include "saturn_gouraud_bank.h"
#include "saturn_texture_residency.h"
#include "saturn_source_runtime.h"
#include "saturn_vdp1_backend.h"
#include "source_cart.h"
#include "source_q16_kernel_probe.h"
#include "source_route_probe.h"
#include "../gpl/slavedriver_dma_queue.h" /* gpl/ is a sibling of sourceboot/
                                           * under src/port/saturn/; matches
                                           * hwtest's existing include style
                                           * since no -I path exposes gpl/
                                           * by bare name (see the Makefile's
                                           * SH_CFLAGS -I list). */

#ifndef SATURN_SOURCEBOOT_ROUTE_REPLAY
#define SATURN_SOURCEBOOT_ROUTE_REPLAY 0
#endif

static sm64_saturn_fast3d_frontend_t sourceboot_fast3d;
static uint32_t sourceboot_sim_ticks_accum;
static uint32_t sourceboot_sim_tick_count;
static sm64_saturn_mario_actor_snapshot_t sourceboot_mario_snapshot;
static sm64_saturn_mario_actor_pose_t sourceboot_mario_pose;
sm64_saturn_source_route_probe_t sourceboot_route_checkpoint;

const sm64_saturn_input_replay_sample_t *
sm64_saturn_sourceboot_bob_parity_v1(uint16_t *sample_count);

static uint16_t sourceboot_frt_delta(uint16_t start, uint16_t end)
{
    return (uint16_t)(end - start);
}

#if SATURN_SOURCEBOOT_ROUTE_REPLAY
static uint32_t sourceboot_float_bits(f32 value) {
    union { f32 f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static void sourceboot_capture_route_checkpoint(void) {
    const sm64_saturn_source_runtime_state_t *runtime =
        sm64_saturn_source_runtime_state();
    const sm64_saturn_fast3d_profile_t *profile = &sourceboot_fast3d.profile;

    /* The capture harness runs a fixed wall-frame budget. Once the replay
     * endpoint is published, later neutral frames must not overwrite the
     * exact deterministic checkpoint with a post-route state (the demo path
     * can finish the route earlier than the interpreted path). */
    if (runtime->input_replay_complete &&
        sourceboot_route_checkpoint.magic ==
            SM64_SATURN_SOURCE_ROUTE_PROBE_MAGIC &&
        sourceboot_route_checkpoint.replay_ticks ==
            runtime->input_replay_ticks) {
        return;
    }

    /* Publish the latest source-frame state. The host accepts it only when
     * replay_ticks is the route's exact 600-tick endpoint, so a stalled
     * controller cadence is reported as a failed gate rather than omitted. */
    sourceboot_route_checkpoint.version = SM64_SATURN_SOURCE_ROUTE_PROBE_VERSION;
    sourceboot_route_checkpoint.replay_ticks = runtime->input_replay_ticks;
    sourceboot_route_checkpoint.global_timer = gGlobalTimer;
    if (gMarioState != NULL) {
        sourceboot_route_checkpoint.mario_action = gMarioState->action;
        sourceboot_route_checkpoint.mario_pos_x_bits = sourceboot_float_bits(gMarioState->pos[0]);
        sourceboot_route_checkpoint.mario_pos_y_bits = sourceboot_float_bits(gMarioState->pos[1]);
        sourceboot_route_checkpoint.mario_pos_z_bits = sourceboot_float_bits(gMarioState->pos[2]);
    } else {
        /* Keep the replay completion observable even if an early bootstrap
         * regression has not produced Mario state yet. The comparator treats
         * these sentinels as a failed BOB route, rather than hiding it. */
        sourceboot_route_checkpoint.mario_action = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_x_bits = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_y_bits = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_z_bits = UINT32_MAX;
    }
    sourceboot_route_checkpoint.camera_mode =
        gCamera == NULL ? UINT32_MAX : (uint32_t)(uint16_t)gCamera->mode;
    sourceboot_route_checkpoint.triangles_transformed = profile->triangles_transformed;
    sourceboot_route_checkpoint.triangles_vdp1_emitted = profile->triangles_vdp1_emitted;
    sourceboot_route_checkpoint.fault_flags = profile->fault_flags;
    sourceboot_route_checkpoint.command_capacity_rejects =
        profile->reject_command_capacity;
    /* Publish last: a host that sees the magic sees a complete snapshot. */
    sourceboot_route_checkpoint.magic = SM64_SATURN_SOURCE_ROUTE_PROBE_MAGIC;
}
#endif

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

#define SOURCEBOOT_VDP1_COMMAND_CAPACITY 2048U
#define SOURCEBOOT_BOB_TEXTURE_BYTES 333696U
#define SOURCEBOOT_BOB_CLUT_COUNT 1077U
#define SOURCEBOOT_BOB_CLUT_BYTES (SOURCEBOOT_BOB_CLUT_COUNT * sizeof(vdp1_clut_t))

extern const uint8_t sm64_saturn_bob_texture_bank[];
extern const uint8_t sm64_saturn_bob_clut_bank[];

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
 * header comment). 1536 * 8 = 12,288 bytes. One table per resolved
 * triangle, rebuilt every frame (bank_begin) and uploaded
 * used-prefix-only after emission -- see saturn_fast3d_vdp1_emit.c.
 *
 * Budget: the live margin is 149,084 bytes (145.6 KiB), measured
 * 2026-07-24 as 0x06100000 - ___end with ___end at 0x060db9a4. Earlier
 * comments here and in saturn_fast3d_frontend.h cited "~191 KiB"; that
 * figure predates several static consumers and was being re-quoted, not
 * re-measured, so successive additions each charged themselves against
 * the same non-decrementing number. Re-measure with sh-elf-nm after any
 * change to static HWRAM, and note that sourceboot-cart.x now enforces
 * a 4 KiB floor at link time for libyaul's TLSF control block. */
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

/* BOB's sky is the VDP2 back screen, not a VDP1 polygon.  The back-screen
 * color table is sampled once per display line, so this costs 224 RGB1555
 * entries in VDP2 VRAM and no work in the game/render loop.  Keep the table
 * in HWRAM until vdp2_scrn_back_sync() queues the upload; it is deliberately
 * a fixed boot asset rather than camera or simulation state. */
#define SOURCEBOOT_BACKSCREEN_LINES 224U
static rgb1555_t sourceboot_sky_gradient[SOURCEBOOT_BACKSCREEN_LINES];

static void sourceboot_init_sky_gradient(void)
{
    for (uint16_t line = 0; line < SOURCEBOOT_BACKSCREEN_LINES; line++) {
        /* Dark blue at the horizon, brighter blue overhead.  RGB1555's
         * channels are 5-bit; interpolate with integer arithmetic so the
         * boot image is deterministic on SH-2 and host probes. */
        const uint16_t t = (uint16_t)(SOURCEBOOT_BACKSCREEN_LINES - 1U - line);
        const uint16_t r = (uint16_t)(1U + (t * 1U) /
            (SOURCEBOOT_BACKSCREEN_LINES - 1U));
        const uint16_t g = (uint16_t)(2U + (t * 8U) /
            (SOURCEBOOT_BACKSCREEN_LINES - 1U));
        const uint16_t b = (uint16_t)(8U + (t * 15U) /
            (SOURCEBOOT_BACKSCREEN_LINES - 1U));
        sourceboot_sky_gradient[line] = RGB1555(1, r, g, b);
    }

    vdp2_scrn_back_buffer_set(VDP2_VRAM_ADDR(3, 0x01FE00),
                              sourceboot_sky_gradient,
                              SOURCEBOOT_BACKSCREEN_LINES);
    vdp2_scrn_back_sync();
}

void user_init(void) {
    /* First, matching both siblings' user_init order (castleviewer
     * main.c:1186, marioturntable main.c:247). */
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                              VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    sourceboot_init_sky_gradient();
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
    /* Keep the one-shot SH-2 kernel vector observable in headless Ymir's
     * no-cart negative-control configuration too: cart loading may fail
     * before the source game loop is available. */
    sm64_saturn_sourceboot_q16_kernel_probe_run();
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
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
    {
        uint16_t sample_count = 0U;
        const sm64_saturn_input_replay_sample_t *route =
            sm64_saturn_sourceboot_bob_parity_v1(&sample_count);
        sm64_saturn_source_runtime_configure_input_replay(route, sample_count);
    }
#endif

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
         * (third_party/libyaul/libyaul/scu/bus/b/vdp/vdp_init.c:50-53
         * and vdp1_vram.c:22-78 in that same submodule), not just the
         * installed headers. Real
         * captured Bob-omb Battlefield free-roam frames run 1,365-1,431
         * triangles (SOURCEBOOT_VDP1_COMMAND_CAPACITY's comment above),
         * so the stock 1024-table cap would push the ORDINARY case into
         * the flat-fallback path, not a rare edge case. Re-partition
         * explicitly so gouraud covers the full MAX_RESOLVED_TRIANGLES
         * (1536) worst case -- see this task's commit message for the
         * full byte-budget arithmetic against VDP1_VRAM_SIZE.
         *
         * Reserve the milestone-1 BOB CLUT16 texture bank and its 1,077
         * per-tile CLUTs. The bake gate measures 333,696 bytes of packed
         * texels plus 34,464 bytes of CLUT data; both partitions are
         * reserved here before runtime binding is introduced at the IR
         * renderer seam. texture_size remains a MULTIPLE OF 8: gouraud_base
         * is laid immediately after the texture region, and an 8-byte-
         * misaligned base is silently truncated by CMDGRDA's >>3 encoding.
         *
         * cmdt_count stays at SOURCEBOOT_VDP1_COMMAND_CAPACITY so
         * Yaul's own bookkeeping matches the size of the command region
         * this backend actually writes (VDP1_VRAM(0) CPU-copy in
         * sm64_saturn_vdp1_backend_upload, bypassing Yaul's
         * partition-aware cmdt allocator entirely -- see
         * saturn_vdp1_backend.h), even though nothing on this path
         * reads partitions.cmdt_base. */
        vdp1_vram_partitions_set(SOURCEBOOT_VDP1_COMMAND_CAPACITY,
                                 SOURCEBOOT_BOB_TEXTURE_BYTES,
                                 SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES,
                                 SOURCEBOOT_BOB_CLUT_COUNT);

        saturn_dma_queue_init();
        vdp1_vram_partitions_get(&partitions);
#if SATURN_DEMO_PATH
        sm64_saturn_demo_render_init();
        /* The baked BOB bank is linked into .cart_rodata and copied to the
         * DRAM cart at its final VMA by source_cart_load(). Stage it through
         * the shared residency API before the first demo-path command list;
         * no frame ever follows a cart pointer. */
        sm64_saturn_texture_residency_t demo_texture_residency;
        sm64_saturn_texture_residency_init(&demo_texture_residency,
                                           &partitions);
        if (!sm64_saturn_texture_residency_upload(
                &demo_texture_residency, 0, sm64_saturn_bob_texture_bank,
                SOURCEBOOT_BOB_TEXTURE_BYTES)) {
            dbgio_puts("sourceboot: BOB texture residency failed\n");
            for (;;) {}
        }
        scu_dma_transfer(0, partitions.clut_base, sm64_saturn_bob_clut_bank,
                         SOURCEBOOT_BOB_CLUT_BYTES);
        scu_dma_transfer_wait(0);
#endif
        /* The backend CPU-copies its command list to VDP1_VRAM(0)
         * without consulting Yaul's partition layout -- verify the
         * gouraud partition clears the command region before trusting
         * it. Overlap => capacity 0 => every triangle takes the
         * counted flat fallback (degradation contract), no crash.
         *
         * The third clause is an alignment guard, and it matters more
         * than it looks: vdp1_cmdt_gouraud_base_set() encodes the table
         * address as (base >> 3) & 0xFFFF (libyaul .../vdp1/cmdt.h:415),
         * so a gouraud_base that is not 8-byte aligned has its low bits
         * SILENTLY TRUNCATED -- every lit primitive would then read its
         * Gouraud table from the wrong address, with no counter, no
         * fallback, and no boot-time complaint. Today's layout is
         * aligned only as a consequence of texture_size being 0; the
         * partition allocator lays gouraud_base immediately after the
         * texture region, so ANY texture budget the next cycle passes
         * here moves it. Fail into the counted flat path instead of
         * corrupting every frame. */
        if ((uintptr_t)partitions.gouraud_base >= cmd_end &&
            ((uintptr_t)partitions.gouraud_base &
                (sizeof(sm64_saturn_gouraud_table_t) - 1U)) == 0U &&
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
        const uint16_t sim_start = cpu_frt_count_get();
        game_loop_one_iteration();
        const uint16_t sim_end = cpu_frt_count_get();
        sourceboot_fast3d.profile.sim_frt_ticks_last =
            sourceboot_frt_delta(sim_start, sim_end);
        sourceboot_sim_ticks_accum +=
            sourceboot_fast3d.profile.sim_frt_ticks_last;
        sourceboot_sim_tick_count++;
        sourceboot_fast3d.profile.sim_frt_ticks_accum =
            sourceboot_sim_ticks_accum;
        sourceboot_fast3d.profile.sim_tick_count = sourceboot_sim_tick_count;

        /* Renderer-facing actor state is captured after the authoritative
         * source tick and before command emission. The bridge is read-only;
         * the eventual IR renderer consumes these records instead of
         * consulting live globals from a transform worker. */
        if (sm64_saturn_mario_actor_snapshot(&sourceboot_mario_snapshot)) {
            (void)sm64_saturn_mario_actor_pose(&sourceboot_mario_snapshot,
                                               &sourceboot_mario_pose);
        }
        sourceboot_fast3d.profile.demo_actor_snapshot_valid =
            sourceboot_mario_snapshot.valid;
        sourceboot_fast3d.profile.demo_actor_pose_vertices =
            sourceboot_mario_pose.vertex_count;

        const uint16_t render_start = cpu_frt_count_get();
#if SATURN_DEMO_PATH
        sm64_saturn_demo_render_frame(&sourceboot_vdp1_backend,
                                      &sourceboot_gouraud_bank,
                                      &sourceboot_fast3d.profile,
                                      &sourceboot_mario_snapshot,
                                      &sourceboot_mario_pose);
#else
        sm64_saturn_fast3d_vdp1_emit(&sourceboot_fast3d,
                                     &sourceboot_vdp1_backend,
                                     &sourceboot_gouraud_bank);
#endif
        sourceboot_fast3d.profile.render_frt_ticks_last =
            sourceboot_frt_delta(render_start, cpu_frt_count_get());
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
        sourceboot_capture_route_checkpoint();
#endif

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
    }
}
