/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>
#include <cpu/cache.h>

#include "game/camera.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_actor_bridge.h"
#include "saturn_demo_render.h"
#include "saturn_gouraud_bank.h"
#include "saturn_math_route_capture.h"
#include "saturn_texture_residency.h"
#include "saturn_source_runtime.h"
#include "saturn_camera_role.h"
#include "saturn_vdp1_backend.h"
#include "saturn_vdp2_frame.h"
#include "source_cart.h"
#include "source_camera_acceptance_route.h"
#include "source_camera_idle_probe.h"
#include "source_q16_kernel_probe.h"
#include "source_route_probe.h"
#include "mario_eye_uv_tiles.h"
#include "../gpl/slavedriver_dma_queue.h" /* gpl/ is a sibling of sourceboot/
                                           * under src/port/saturn/; matches
                                           * hwtest's existing include style
                                           * since no -I path exposes gpl/
                                           * by bare name (see the Makefile's
                                           * SH_CFLAGS -I list). */

#ifndef SATURN_SOURCEBOOT_ROUTE_REPLAY
#define SATURN_SOURCEBOOT_ROUTE_REPLAY 0
#endif
#ifndef SATURN_DEMO_PATH
#define SATURN_DEMO_PATH 0
#endif
#ifndef SATURN_EXPERIMENTAL_SKIP_GEO_WALK
#define SATURN_EXPERIMENTAL_SKIP_GEO_WALK 0
#endif
#ifndef SATURN_SOURCEBOOT_CAMERA_ROUTE
#define SATURN_SOURCEBOOT_CAMERA_ROUTE 0
#endif
#ifndef SATURN_SOURCEBOOT_LIVE_INPUT
#define SATURN_SOURCEBOOT_LIVE_INPUT 0
#endif

#define SOURCEBOOT_SIM_VBLANK_DIVISOR 2U
#define SOURCEBOOT_MAX_SIM_CATCHUP 2U
#define SOURCEBOOT_BOOT_TRACE_MAGIC 0x53394254U
#define SOURCEBOOT_BOOT_TRACE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t stage;
    uint32_t stage_id;
    uint32_t observed_vblank_generation;
    uint32_t scheduler_credit;
    uint32_t vdp1_presentation_generation;
    uint32_t vdp2_presentation_generation;
} sm64_saturn_sourceboot_boot_trace_t;

enum {
    SOURCEBOOT_BOOT_TRACE_STAGE_MAIN_ENTRY = 1U,
    SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_RETIRED,
    SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_AFTER,
    SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER,
    SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_AFTER,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_AFTER,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_BEFORE,
    SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_AFTER,
};

/* Deliberately non-static: headless Ymir resolves this symbol from the ELF
 * and reads the record from target RAM after BIOS handoff.  `stage` is a
 * monotonically advancing publication sequence; `stage_id` names the last
 * boundary reached, so repeated frame-loop stages remain distinguishable. */
volatile sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace;

static sm64_saturn_fast3d_frontend_t sourceboot_fast3d;
static uint32_t sourceboot_sim_ticks_accum;
static uint32_t sourceboot_sim_tick_count;
static uint32_t sourceboot_render_ticks_accum;
static volatile uint32_t sourceboot_vblank_out_count;
static uint32_t sourceboot_sim_vblank_credit_dropped;
static uint32_t sourceboot_vdp1_bank_generation;
static uint32_t sourceboot_vdp1_bank_submitted;
static uint32_t sourceboot_vdp1_bank_displayed;
static uint32_t sourceboot_vdp1_bank_overwrite_attempts;
static uint32_t sourceboot_vdp1_bank_late_dma;
static uint32_t sourceboot_dma_wait_ticks_accum;
static uint32_t sourceboot_vdp1_wait_ticks_accum;
static uint32_t sourceboot_trace_scheduler_credit;
static uint32_t sourceboot_trace_vdp1_presentation_generation;
static uint32_t sourceboot_trace_vdp2_presentation_generation;
static sm64_saturn_mario_actor_snapshot_t sourceboot_mario_snapshot;
static sm64_saturn_mario_actor_pose_t sourceboot_mario_pose;
static sm64_saturn_vdp2_frame_t sourceboot_vdp2_frame;
sm64_saturn_source_route_probe_t sourceboot_route_checkpoint;
sm64_saturn_camera_timing_t sm64_saturn_camera_timing;
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
volatile sm64_saturn_math_route_capture_t sourceboot_math_route_capture;
#endif

const sm64_saturn_input_replay_sample_t *
sm64_saturn_sourceboot_bob_parity_v1(uint16_t *sample_count);

static void sourceboot_boot_trace_write(uint32_t stage_id,
                                        uint32_t observed_vblank_generation)
{
    sourceboot_boot_trace.magic = SOURCEBOOT_BOOT_TRACE_MAGIC;
    sourceboot_boot_trace.version = SOURCEBOOT_BOOT_TRACE_VERSION;
    sourceboot_boot_trace.observed_vblank_generation =
        observed_vblank_generation;
    sourceboot_boot_trace.scheduler_credit =
        sourceboot_trace_scheduler_credit;
    sourceboot_boot_trace.vdp1_presentation_generation =
        sourceboot_trace_vdp1_presentation_generation;
    sourceboot_boot_trace.vdp2_presentation_generation =
        sourceboot_trace_vdp2_presentation_generation;
    sourceboot_boot_trace.stage_id = stage_id;
    sourceboot_boot_trace.stage++;
}

static uint16_t sourceboot_frt_delta(uint16_t start, uint16_t end)
{
    return (uint16_t)(end - start);
}

static void sourceboot_run_source_tick(void)
{
    const uint16_t sim_start = cpu_frt_count_get();
#if SATURN_DEMO_PATH
    /* Keep final display submission suppressed while the IR demo owns the
     * frame.  Do not enable scene-graph suppression here: geo_process_root()
     * still owns source animation and stateful geo-callback updates. */
    sm64_saturn_source_runtime_set_display_suppressed(true);
#endif
#if SATURN_EXPERIMENTAL_SKIP_GEO_WALK
    /* Sealed upper-bound diagnostic only: geo-owned animation, warp, camera,
     * water, moving-texture, carpet, and matrix state are intentionally invalid. */
    sm64_saturn_source_runtime_set_scene_graph_suppressed(true);
    game_loop_one_iteration();
    sm64_saturn_source_runtime_set_scene_graph_suppressed(false);
#else
    game_loop_one_iteration();
#endif
#if SATURN_DEMO_PATH
    sm64_saturn_source_runtime_set_display_suppressed(false);
#endif
#if SATURN_DEMO_PATH
    /* This is deliberately inside the authoritative-tick helper, not after
     * the catch-up batch: an exit plus same-ID re-entry can otherwise be
     * invisible if multiple source ticks complete before one render. */
    sm64_saturn_demo_render_scene_observe(gCurrentArea != NULL,
                                          gCurrLevelNum, gCurrAreaIndex);
#endif
    const uint16_t sim_end = cpu_frt_count_get();
    sourceboot_fast3d.profile.sim_frt_ticks_last =
        sourceboot_frt_delta(sim_start, sim_end);
    sourceboot_sim_ticks_accum +=
        sourceboot_fast3d.profile.sim_frt_ticks_last;
    sourceboot_sim_tick_count++;
    sourceboot_fast3d.profile.sim_frt_ticks_accum =
        sourceboot_sim_ticks_accum;
    sourceboot_fast3d.profile.sim_tick_count = sourceboot_sim_tick_count;
    sourceboot_fast3d.profile.scene_graph_walks =
        sm64_saturn_source_runtime_state()->scene_graph_walks;
    sourceboot_fast3d.profile.scene_graph_walks_suppressed =
        sm64_saturn_source_runtime_state()->scene_graph_walks_suppressed;
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1 && !SATURN_SOURCEBOOT_LIVE_INPUT
    if (sm64_saturn_source_runtime_state()->input_replay_complete) {
        sm64_saturn_camera_bypass_arm(sourceboot_sim_tick_count);
    }
#endif
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1 && !SATURN_SOURCEBOOT_LIVE_INPUT
    sm64_saturn_sourceboot_camera_idle_probe_record(
        sm64_saturn_source_runtime_state(),
        sourceboot_sim_tick_count);
#endif
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
     * replay_ticks is the route's exact configured endpoint, so a stalled
     * controller cadence is reported as a failed gate rather than omitted. */
    sourceboot_route_checkpoint.version = SM64_SATURN_SOURCE_ROUTE_PROBE_VERSION;
    sourceboot_route_checkpoint.atan2_variant = SATURN_ATAN2_VARIANT;
    sourceboot_route_checkpoint.replay_ticks = runtime->input_replay_ticks;
    sourceboot_route_checkpoint.global_timer = gGlobalTimer;
    if (gMarioState != NULL) {
        sourceboot_route_checkpoint.mario_action = gMarioState->action;
        sourceboot_route_checkpoint.mario_pos_x_bits = sourceboot_float_bits(gMarioState->pos[0]);
        sourceboot_route_checkpoint.mario_pos_y_bits = sourceboot_float_bits(gMarioState->pos[1]);
        sourceboot_route_checkpoint.mario_pos_z_bits = sourceboot_float_bits(gMarioState->pos[2]);
        sourceboot_route_checkpoint.mario_face_angle_x =
            (uint32_t)(uint16_t)gMarioState->faceAngle[0];
        sourceboot_route_checkpoint.mario_face_angle_y =
            (uint32_t)(uint16_t)gMarioState->faceAngle[1];
        sourceboot_route_checkpoint.mario_face_angle_z =
            (uint32_t)(uint16_t)gMarioState->faceAngle[2];
    } else {
        /* Keep the replay completion observable even if an early bootstrap
         * regression has not produced Mario state yet. The comparator treats
         * these sentinels as a failed BOB route, rather than hiding it. */
        sourceboot_route_checkpoint.mario_action = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_x_bits = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_y_bits = UINT32_MAX;
        sourceboot_route_checkpoint.mario_pos_z_bits = UINT32_MAX;
        sourceboot_route_checkpoint.mario_face_angle_x = UINT32_MAX;
        sourceboot_route_checkpoint.mario_face_angle_y = UINT32_MAX;
        sourceboot_route_checkpoint.mario_face_angle_z = UINT32_MAX;
    }
    if (gCamera != NULL) {
        sourceboot_route_checkpoint.camera_pos_x_bits =
            sourceboot_float_bits(gCamera->pos[0]);
        sourceboot_route_checkpoint.camera_pos_y_bits =
            sourceboot_float_bits(gCamera->pos[1]);
        sourceboot_route_checkpoint.camera_pos_z_bits =
            sourceboot_float_bits(gCamera->pos[2]);
    } else {
        sourceboot_route_checkpoint.camera_pos_x_bits = UINT32_MAX;
        sourceboot_route_checkpoint.camera_pos_y_bits = UINT32_MAX;
        sourceboot_route_checkpoint.camera_pos_z_bits = UINT32_MAX;
    }
    sourceboot_route_checkpoint.camera_mode =
        gCamera == NULL ? UINT32_MAX : (uint32_t)(uint16_t)gCamera->mode;
    sourceboot_route_checkpoint.triangles_transformed = profile->triangles_transformed;
    sourceboot_route_checkpoint.triangles_emitted = profile->triangles_emitted;
    sourceboot_route_checkpoint.triangles_vdp1_emitted = profile->triangles_vdp1_emitted;
    sourceboot_route_checkpoint.reject_near_far = profile->reject_near_far;
    sourceboot_route_checkpoint.reject_backface = profile->reject_backface;
    sourceboot_route_checkpoint.reject_degenerate = profile->reject_degenerate;
    sourceboot_route_checkpoint.reject_vertex_range = profile->reject_vertex_range;
    sourceboot_route_checkpoint.reject_command_capacity =
        profile->reject_command_capacity;
    sourceboot_route_checkpoint.reject_vdp1_arena_capacity =
        profile->reject_vdp1_arena_capacity;
    sourceboot_route_checkpoint.reject_w_nonpositive =
        profile->reject_w_nonpositive;
    sourceboot_route_checkpoint.reject_z_near = profile->reject_z_near;
    sourceboot_route_checkpoint.reject_z_far = profile->reject_z_far;
    sourceboot_route_checkpoint.reject_offscreen = profile->reject_offscreen;
    sourceboot_route_checkpoint.reject_span = profile->reject_span;
    sourceboot_route_checkpoint.reject_w_nonpositive_overflow_suspect =
        profile->reject_w_nonpositive_overflow_suspect;
    sourceboot_route_checkpoint.fault_flags = profile->fault_flags;
    sourceboot_route_checkpoint.frame_serial = profile->frame_serial;
    sourceboot_route_checkpoint.sim_frt_ticks_accum = profile->sim_frt_ticks_accum;
    sourceboot_route_checkpoint.render_frt_ticks_accum = profile->render_frt_ticks_accum;
    sourceboot_route_checkpoint.render_frt_ticks_last = profile->render_frt_ticks_last;
    sourceboot_route_checkpoint.master_wait_ticks = profile->master_wait_ticks;
    sourceboot_route_checkpoint.slave_busy_ticks = profile->slave_busy_ticks;
    sourceboot_route_checkpoint.slave_jobs_completed = profile->slave_jobs_completed;
    sourceboot_route_checkpoint.slave_timeouts = profile->slave_timeouts;
    sourceboot_route_checkpoint.camera_ticks_last = sm64_saturn_camera_timing.ticks_last;
    sourceboot_route_checkpoint.camera_ticks_accum = sm64_saturn_camera_timing.ticks_accum;
    sourceboot_route_checkpoint.camera_invocations = sm64_saturn_camera_timing.invocations;
    sourceboot_route_checkpoint.camera_ticks_max = sm64_saturn_camera_timing.ticks_max;
    sourceboot_math_route_capture.version = SM64_SATURN_MATH_ROUTE_CAPTURE_VERSION;
    sourceboot_math_route_capture.replay_ticks = runtime->input_replay_ticks;
    /* Publish after every other field so a host that sees magic can trust the
     * same source tick's counters and input bits. */
    sourceboot_math_route_capture.magic = SM64_SATURN_MATH_ROUTE_CAPTURE_MAGIC;
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
#if defined(SATURN_DEMO_BSP_FRAGMENTS) && SATURN_DEMO_BSP_FRAGMENTS
#define SOURCEBOOT_BOB_TEXTURE_BYTES 261248U
#define SOURCEBOOT_BOB_CLUT_COUNT 2041U
#else
#define SOURCEBOOT_BOB_TEXTURE_BYTES 333696U
#define SOURCEBOOT_BOB_CLUT_COUNT 1077U
#endif
#define SOURCEBOOT_MARIO_TEXTURE_BYTES \
    (SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT * \
     SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * \
     sizeof(uint16_t))
#define SOURCEBOOT_TEXTURE_BYTES \
    (SOURCEBOOT_BOB_TEXTURE_BYTES + SOURCEBOOT_MARIO_TEXTURE_BYTES)
#define SOURCEBOOT_BOB_CLUT_BYTES (SOURCEBOOT_BOB_CLUT_COUNT * sizeof(vdp1_clut_t))

#if defined(SATURN_DEMO_BSP_FRAGMENTS) && SATURN_DEMO_BSP_FRAGMENTS
extern const uint8_t sm64_saturn_bob_fragment_texture_bank[];
extern const uint8_t sm64_saturn_bob_fragment_clut_bank[];
#define sm64_saturn_bob_texture_bank sm64_saturn_bob_fragment_texture_bank
#define sm64_saturn_bob_clut_bank sm64_saturn_bob_fragment_clut_bank
#else
extern const uint8_t sm64_saturn_bob_texture_bank[];
extern const uint8_t sm64_saturn_bob_clut_bank[];
#endif

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
static vdp1_cmdt_t sourceboot_vdp1_cmdts[2][SOURCEBOOT_VDP1_COMMAND_CAPACITY]
    __attribute__((section(".lwram_cmdts")));
static sm64_saturn_vdp1_backend_t sourceboot_vdp1_backend;
static uint8_t sourceboot_vdp1_cmdts_bank;

/* HWRAM (.bss) deliberately: SCU DMA from LWRAM is the documented
 * lockup class the VDP1 backend above already works around (see its
 * header comment). Each 1536 * 8 = 12,288-byte staging bank is rebuilt
 * while the other VDP1 frame is being consumed. The queue retains only an
 * address/length descriptor, so a bank is never reused before its matching
 * DMA completion sequence has retired.
 *
 * Budget: the live margin is 149,084 bytes (145.6 KiB), measured
 * 2026-07-24 as 0x06100000 - ___end with ___end at 0x060db9a4. Earlier
 * comments here and in saturn_fast3d_frontend.h cited "~191 KiB"; that
 * figure predates several static consumers and was being re-quoted, not
 * re-measured, so successive additions each charged themselves against
 * the same non-decrementing number. Re-measure with sh-elf-nm after any
 * change to static HWRAM, and note that sourceboot-cart.x now enforces
 * a 4 KiB floor at link time for libyaul's TLSF control block. */
static sm64_saturn_gouraud_table_t sourceboot_gouraud_staging[2]
    [SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
static sm64_saturn_gouraud_bank_t sourceboot_gouraud_banks[2];

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
    sourceboot_vblank_out_count++;
    smpc_peripheral_intback_issue();
}

/* BOB's sky is the VDP2 back screen, not a VDP1 polygon.  The back-screen
 * color table is sampled once per display line, so this costs 224 RGB1555
 * entries in VDP2 VRAM and no work in the game/render loop.  Keep the table
 * in HWRAM until vdp2_scrn_back_sync() queues the upload; it is deliberately
 * a fixed boot asset rather than camera or simulation state. */
#define SOURCEBOOT_BACKSCREEN_LINES 224U
static rgb1555_t sourceboot_sky_gradient[SOURCEBOOT_BACKSCREEN_LINES];

#define SOURCEBOOT_SKY_BITMAP_WIDTH 512U
#define SOURCEBOOT_SKY_BITMAP_HEIGHT 256U
#define SOURCEBOOT_SKY_BITMAP_WORDS \
    (SOURCEBOOT_SKY_BITMAP_WIDTH * SOURCEBOOT_SKY_BITMAP_HEIGHT)
#define SOURCEBOOT_VDP2_DISPLAY_MASK SM64_SATURN_VDP2_FRAME_DISPLAY_MASK
#define SOURCEBOOT_VDP2_VRAM_BYTES \
    ((SOURCEBOOT_SKY_BITMAP_WORDS * sizeof(uint16_t)) + \
     (SOURCEBOOT_BACKSCREEN_LINES * sizeof(rgb1555_t)))
extern const uint16_t sm64_saturn_bob_sky_bitmap[];

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

static void sourceboot_init_sky_bitmap(void)
{
    volatile uint16_t * const vram = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | VDP2_VRAM_ADDR(0, 0x00000));
    for (uint32_t index = 0; index < SOURCEBOOT_SKY_BITMAP_WORDS; index++) {
        vram[index] = sm64_saturn_bob_sky_bitmap[index];
    }
    const vdp2_scrn_bitmap_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG1,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = VDP2_VRAM_ADDR(0, 0x00000),
    };
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
}

static void sourceboot_vdp2_sky_scroll_set(int32_t x, int32_t y,
                                           void *work __unused)
{
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG1, FIX16(x));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG1, FIX16(y));
}

static void sourceboot_vdp2_hud_write(const char *text, void *work __unused)
{
    dbgio_puts("\x1B[6;1H");
    dbgio_puts(text);
    dbgio_flush();
}

static void sourceboot_vdp2_layers_set(uint32_t display_mask,
                                       uint8_t vdp1_priority,
                                       void *work __unused)
{
    /* NBG1 is an opaque baked sky and must remain behind VDP1. NBG3 hosts
     * dbgio's text. Every sprite group stays visible above both. */
    for (uint8_t priority = 0U; priority < 8U; priority++)
        vdp2_sprite_priority_set(priority, vdp1_priority);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 0U);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG3, 7U);
    vdp2_scrn_display_set((uint16_t)display_mask);
}

static void sourceboot_vdp2_vblank_commit(void *work __unused)
{
    /* Yaul queues shadow state here and commits it in its VBlank-IN path. */
    vdp2_sync();
}

static const sm64_saturn_vdp2_frame_backend_t sourceboot_vdp2_backend = {
    .sky_scroll_set = sourceboot_vdp2_sky_scroll_set,
    .hud_write = sourceboot_vdp2_hud_write,
    .layers_set = sourceboot_vdp2_layers_set,
    .vblank_commit = sourceboot_vdp2_vblank_commit,
    .work = NULL,
};

static sm64_saturn_vdp2_camera_snapshot_t
sourceboot_vdp2_camera_snapshot(void)
{
    return (sm64_saturn_vdp2_camera_snapshot_t){
        .yaw = sourceboot_mario_snapshot.camera_yaw,
        .pitch = sourceboot_mario_snapshot.camera_pitch,
        .valid = sourceboot_mario_snapshot.valid,
    };
}

/* The master alone converts one observed VBlank generation into one VDP1
 * plot and one VDP2 composition commit.  No geometry enters the VDP2 API;
 * its frame is prepared only after VDP1's terminal completion boundary. */
static void sourceboot_present_generation(uint32_t presentation_generation)
{
    const uint16_t vdp1_wait_start = cpu_frt_count_get();
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_BEFORE,
        presentation_generation);
    vdp1_sync_render();
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_RENDER_AFTER,
        presentation_generation);
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_BEFORE,
                                presentation_generation);
    vdp1_sync();
    sourceboot_trace_vdp1_presentation_generation = presentation_generation;
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_VDP1_SYNC_AFTER,
                                presentation_generation);
    sourceboot_fast3d.profile.vdp1_wait_ticks_last =
        sourceboot_frt_delta(vdp1_wait_start, cpu_frt_count_get());
    sourceboot_vdp1_wait_ticks_accum +=
        sourceboot_fast3d.profile.vdp1_wait_ticks_last;
    sourceboot_fast3d.profile.vdp1_wait_ticks_accum =
        sourceboot_vdp1_wait_ticks_accum;
    const sm64_saturn_vdp2_camera_snapshot_t vdp2_camera =
        sourceboot_vdp2_camera_snapshot();
    sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, &vdp2_camera,
                                 &sourceboot_fast3d.profile,
                                 sourceboot_sim_tick_count);
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_BEFORE,
        presentation_generation);
    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,
                                  &sourceboot_vdp2_backend);
    sourceboot_trace_vdp2_presentation_generation = presentation_generation;
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_VDP2_COMMIT_AFTER,
        presentation_generation);
    sourceboot_vdp1_bank_generation = presentation_generation;
    sourceboot_vdp1_bank_submitted = presentation_generation;
    sourceboot_fast3d.profile.vdp1_bank_generation = presentation_generation;
    sourceboot_fast3d.profile.vdp1_bank_submitted = presentation_generation;
    sourceboot_fast3d.profile.vblank_presentation_generation =
        presentation_generation;
    sourceboot_fast3d.profile.sim_vblank_credit_dropped =
        sourceboot_sim_vblank_credit_dropped;
}

void user_init(void) {
    /* First, matching both siblings' user_init order (castleviewer
     * main.c:1186, marioturntable main.c:247). */
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                              VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    /* The demo can submit more VDP1 work than one video field can retire.
     * Yaul's default auto interval marks a list committed at VBLANK-IN
     * without checking EDSR.CEF, so a long plot can still be reading command
     * VRAM when the next frame uploads over it.  Variable interval -1 is
     * uncapped (frame_rate 0): vdp1_sync_render() starts the plot, and Yaul
     * requests the framebuffer change only after EDSR.CEF reports draw-end.
     * This restores the complete-frame boundary used by the pinned
     * Z-Treme/SGL slSynch loop without imposing a second unconditional
     * VBlank wait on sourceboot's stock game pacing. */
    vdp1_sync_interval_set(-1);
    sourceboot_init_sky_gradient();
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
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_MAIN_ENTRY, 0U);
    sm64_saturn_sourceboot_q16_kernel_probe_run();
    const sm64_saturn_source_cart_status_t cart_status =
        sm64_saturn_source_cart_load();
    if (cart_status != SM64_SATURN_SOURCE_CART_OK) {
        sm64_saturn_source_cart_report_failure(cart_status);
        for (;;) {}
    }
    /* The bitmap is linked in .cart_rodata and is not readable from its
     * final DRAM-cart address until source_cart_load() has completed. Keep
     * the VDP2 format setup in user_init(), but defer the actual copy so NBG1
     * never receives a zeroed pre-cart buffer. */
    sourceboot_init_sky_bitmap();
    sm64_saturn_vdp2_frame_init(&sourceboot_vdp2_frame);

    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    dbgio_puts("\x1B[H\x1B[2JSM64 SATURN SOURCEBOOT E2\n"
               "Direct original Bob script\n"
               "SOURCE.DAT -> 4 MiB RAM cart\n"
               "Source loop -> Fast3D task intake\n");
    dbgio_flush();
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_BEFORE, 0U);
    sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, NULL,
                                 &sourceboot_fast3d.profile,
                                 sourceboot_sim_tick_count);
    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,
                                  &sourceboot_vdp2_backend);
    vdp2_sync_wait();
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_RETIRED, 0U);
    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);
#if SATURN_DEMO_PATH
    /* The demo renderer consumes the authoritative source state through its
     * IR bridge below. Keep the original exec_display_list symbol reachable
     * for the source ABI, but do not submit every source display list to the
     * interpreted frontend as well: doing both doubled the render work and
     * violated the demo-path frame-loop contract. */
    sm64_saturn_source_runtime_configure(NULL, NULL);
#else
    sm64_saturn_source_runtime_configure(sm64_saturn_fast3d_frontend_submit,
                                         &sourceboot_fast3d);
#endif
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
    {
        uint16_t sample_count = 0U;
        const sm64_saturn_input_replay_sample_t *route;
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1
        route = sm64_saturn_sourceboot_bob_default_camera_v1(&sample_count);
#else
        route =
            sm64_saturn_sourceboot_bob_parity_v1(&sample_count);
#endif
        sm64_saturn_source_runtime_configure_input_replay(route, sample_count);
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1
        sm64_saturn_sourceboot_camera_idle_probe_reset();
#endif
        sm64_saturn_camera_timing_reset();
    }
#endif

    {
        const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init_with_storage(
                &sourceboot_vdp1_backend, sourceboot_vdp1_cmdts[0],
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
                                 SOURCEBOOT_TEXTURE_BYTES,
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
        /* Initialize the second bank's fixed system/local commands without
         * making it the active list yet. Both banks are independently valid
         * VDP1 lists before the first frame swap. */
        sm64_saturn_vdp1_backend_t spare_backend;
        const int16_vec2_t spare_clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t spare_local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init_with_storage(
                &spare_backend, sourceboot_vdp1_cmdts[1],
                SOURCEBOOT_VDP1_COMMAND_CAPACITY, spare_clip, spare_local)) {
            dbgio_puts("sourceboot: spare VDP1 backend init failed\n");
            dbgio_flush();
            for (;;) {}
        }
        if (!sm64_saturn_texture_residency_upload(
                &demo_texture_residency, SOURCEBOOT_BOB_TEXTURE_BYTES,
                sm64_mario_texture_uv_tiles,
                SOURCEBOOT_MARIO_TEXTURE_BYTES)) {
            dbgio_puts("sourceboot: Mario texture residency failed\n");
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
        for (uint8_t bank = 0U; bank < 2U; bank++) {
            (void)sm64_saturn_gouraud_bank_init(
                &sourceboot_gouraud_banks[bank],
                sourceboot_gouraud_staging[bank], capacity,
                (uintptr_t)partitions.gouraud_base);
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

    /* These are the source-port calls used by src/pc/pc_main.c. The
     * interpreted build keeps its original one-call-per-loop behavior. The
     * demo build instead schedules authoritative source ticks at 30 Hz and
     * suppresses only the source display-list submission while it catches up
     * after a slow IR render. */
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_BEFORE,
                                sourceboot_vblank_out_count);
    thread5_game_loop(NULL);
    uint32_t scheduler_vblank_clock = sourceboot_vblank_out_count;
    uint32_t sourceboot_presentation_generation = scheduler_vblank_clock;
    uint32_t sim_vblank_credit = SOURCEBOOT_SIM_VBLANK_DIVISOR;
    sourceboot_trace_scheduler_credit = sim_vblank_credit;
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_AFTER,
                                scheduler_vblank_clock);
    for (;;) {
        /* Sample the ISR-owned VBlank clock exactly once before any source
         * tick.  A tick can take longer than a field, but it cannot refill
         * this generation's credit or trigger a second presentation. */
#if SATURN_DEMO_PATH
        bool simulation_ran = false;
#endif
        uint32_t scheduler_now = sourceboot_vblank_out_count;
        sim_vblank_credit += scheduler_now - scheduler_vblank_clock;
        scheduler_vblank_clock = scheduler_now;
        sourceboot_trace_scheduler_credit = sim_vblank_credit;
        if (scheduler_now == sourceboot_presentation_generation) {
            /* No completed fresh field: retain the previously completed
             * VDP1 list and wait rather than rebuilding/uploading/syncing. */
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE,
                scheduler_now);
            sm64_saturn_source_runtime_wait_vblank();
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER,
                scheduler_now);
            continue;
        }
        sourceboot_presentation_generation = scheduler_now;
        for (uint8_t catchup = 0U;
             sim_vblank_credit >= SOURCEBOOT_SIM_VBLANK_DIVISOR &&
             catchup < SOURCEBOOT_MAX_SIM_CATCHUP; catchup++) {
            sim_vblank_credit -= SOURCEBOOT_SIM_VBLANK_DIVISOR;
            sourceboot_trace_scheduler_credit = sim_vblank_credit;
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE,
                scheduler_now);
            sourceboot_run_source_tick();
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER,
                scheduler_now);
#if SATURN_DEMO_PATH
            simulation_ran = true;
#endif
        }
        /* The first tick is normal; the second is bounded recovery. Preserve
         * a fractional VBlank remainder but discard every additional eligible
         * tick so a slow render cannot create a feedback backlog. */
        if (sim_vblank_credit >= SOURCEBOOT_SIM_VBLANK_DIVISOR) {
            const uint32_t dropped_vblank_credit =
                sim_vblank_credit -
                (sim_vblank_credit % SOURCEBOOT_SIM_VBLANK_DIVISOR);
            sourceboot_sim_vblank_credit_dropped +=
                dropped_vblank_credit;
            sim_vblank_credit -= dropped_vblank_credit;
        }
        sourceboot_trace_scheduler_credit = sim_vblank_credit;

        /* Renderer-facing actor state is captured after the authoritative
         * source tick and before command emission. The bridge is read-only;
         * the eventual IR renderer consumes these records instead of
         * consulting live globals from a transform worker. */
#if SATURN_DEMO_PATH
        if (simulation_ran &&
            sm64_saturn_mario_actor_snapshot(&sourceboot_mario_snapshot)) {
            (void)sm64_saturn_mario_actor_pose(&sourceboot_mario_snapshot,
                                               &sourceboot_mario_pose);
        }
#else
        if (sm64_saturn_mario_actor_snapshot(&sourceboot_mario_snapshot)) {
            (void)sm64_saturn_mario_actor_pose(&sourceboot_mario_snapshot,
                                               &sourceboot_mario_pose);
        }
#endif
        sourceboot_fast3d.profile.demo_actor_snapshot_valid =
            sourceboot_mario_snapshot.valid;
        sourceboot_fast3d.profile.demo_actor_pose_vertices =
            sourceboot_mario_pose.vertex_count;

        const uint16_t render_start = cpu_frt_count_get();
        /* SlaveDriver and Z-Treme both rebuild a staging bank while VDP1
         * consumes the other frame. Command/Gouraud staging is double
         * buffered here, while VDP1's final VRAM ranges deliberately stay
         * master-owned and single. The renderer waits only at that VRAM
         * overwrite/draw-dependency boundary after CPU construction. */
        const bool vdp1_was_busy = vdp1_sync_busy();
        if (vdp1_was_busy)
            sourceboot_vdp1_bank_late_dma++;
        sourceboot_vdp1_cmdts_bank ^= 1U;
        sm64_saturn_vdp1_backend_bind_storage(
            &sourceboot_vdp1_backend,
            sourceboot_vdp1_cmdts[sourceboot_vdp1_cmdts_bank],
            SOURCEBOOT_VDP1_COMMAND_CAPACITY);
#if SATURN_DEMO_PATH
        sm64_saturn_demo_render_frame(&sourceboot_vdp1_backend,
                                      &sourceboot_gouraud_banks[
                                          sourceboot_vdp1_cmdts_bank],
                                      &sourceboot_fast3d.profile,
                                      &sourceboot_mario_snapshot,
                                      &sourceboot_mario_pose);
#else
        sm64_saturn_fast3d_vdp1_emit(&sourceboot_fast3d,
                                     &sourceboot_vdp1_backend,
                                     &sourceboot_gouraud_banks[
                                         sourceboot_vdp1_cmdts_bank]);
#endif
        /* Both emit paths wait for their just-submitted Gouraud sequence and
         * then upload the final command list. Returning here proves the old
         * list has retired before the corresponding single VDP1 VRAM ranges
         * were overwritten. */
        sourceboot_vdp1_bank_displayed = sourceboot_vdp1_bank_submitted;
        sourceboot_fast3d.profile.render_frt_ticks_last =
            sourceboot_frt_delta(render_start, cpu_frt_count_get());
        sourceboot_render_ticks_accum +=
            sourceboot_fast3d.profile.render_frt_ticks_last;
        sourceboot_fast3d.profile.render_frt_ticks_accum =
            sourceboot_render_ticks_accum;
        sourceboot_fast3d.profile.vdp1_commands_last =
            sourceboot_vdp1_backend.list.count;
        sourceboot_fast3d.profile.vdp1_commands =
            sourceboot_vdp1_backend.list.count;
        sourceboot_fast3d.profile.vdp1_bank_displayed =
            sourceboot_vdp1_bank_displayed;
        sourceboot_fast3d.profile.vdp1_bank_overwrite_attempts =
            sourceboot_vdp1_bank_overwrite_attempts;
        sourceboot_fast3d.profile.vdp1_bank_late_dma =
            sourceboot_vdp1_bank_late_dma;
        sourceboot_fast3d.profile.ordering_count =
            sourceboot_fast3d.profile.vdp1_commands_last;
        sourceboot_fast3d.profile.dma_wait_ticks_last =
            saturn_dma_queue_wait_ticks_take();
        sourceboot_dma_wait_ticks_accum +=
            sourceboot_fast3d.profile.dma_wait_ticks_last;
        sourceboot_fast3d.profile.dma_wait_ticks_accum =
            sourceboot_dma_wait_ticks_accum;
        if (sourceboot_fast3d.profile.vdp1_commands_last >
            sourceboot_fast3d.profile.vdp1_command_highwater)
            sourceboot_fast3d.profile.vdp1_command_highwater =
                sourceboot_fast3d.profile.vdp1_commands_last;
        const uint32_t gouraud_highwater =
            sm64_saturn_gouraud_bank_used_bytes(
                &sourceboot_gouraud_banks[sourceboot_vdp1_cmdts_bank]) /
            sizeof(sm64_saturn_gouraud_table_t);
        if (gouraud_highwater > sourceboot_fast3d.profile.vdp1_gouraud_highwater)
            sourceboot_fast3d.profile.vdp1_gouraud_highwater = gouraud_highwater;
        sourceboot_fast3d.profile.demo_lod_resident_bytes =
            SOURCEBOOT_TEXTURE_BYTES + SOURCEBOOT_BOB_CLUT_BYTES;
        sourceboot_fast3d.profile.vdp1_vram_bytes =
            (SOURCEBOOT_VDP1_COMMAND_CAPACITY * sizeof(vdp1_cmdt_t)) +
            SOURCEBOOT_TEXTURE_BYTES + SOURCEBOOT_BOB_CLUT_BYTES +
            (SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES *
             sizeof(vdp1_gouraud_table_t));
        sourceboot_fast3d.profile.vdp2_display_mask =
            SOURCEBOOT_VDP2_DISPLAY_MASK;
        sourceboot_fast3d.profile.vdp2_active_layers =
            SOURCEBOOT_VDP2_DISPLAY_MASK;
        sourceboot_fast3d.profile.vdp2_vram_bytes =
            SOURCEBOOT_VDP2_VRAM_BYTES;
#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT
    sourceboot_capture_route_checkpoint();
#endif

        sourceboot_present_generation(scheduler_now);
    }
}
