/* E2 sourceboot target: original game-loop ownership, direct Bob source entry. */
#include <yaul.h>
#include <cpu/cache.h>

#include "game/camera.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/hud.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_actor_bridge.h"
#include "saturn_actor_instance.h"
#include "saturn_actor_batch.h"
#include "saturn_render_snapshot.h"
#include "saturn_transform.h"
#include "saturn_demo_render.h"
#include "saturn_gouraud_bank.h"
#include "saturn_math_route_capture.h"
#include "saturn_texture_residency.h"
#include "saturn_source_runtime.h"
#include "saturn_frame_pipeline.h"
#include "saturn_render_overlap_phase.h"
#include "saturn_render_job_runtime.h"
#include "saturn_camera_role.h"
#include "saturn_vdp1_backend.h"
#include "saturn_vdp1_frame_bank.h"
#include "saturn_vdp2_frame.h"
#include "saturn_hud.h"
#include "saturn_hud_atlas.h"
#include "saturn_hud_layout.h"
#include "saturn_hud_publish.h"
#include "saturn_build_identity.h"
#include "source_cart.h"
#include "source_camera_acceptance_route.h"
#include "source_camera_idle_probe.h"
#include "source_q16_kernel_probe.h"
#include "source_route_probe.h"
#include "mario_eye_uv_tiles.h"
#include "saturn_sky_gradient_generated.h"
#include "../gpl/slavedriver_dma_queue.h" /* gpl/ is a sibling of sourceboot/
                                           * under src/port/saturn/; matches
                                           * hwtest's existing include style
                                           * since no -I path exposes gpl/
                                           * by bare name (see the Makefile's
                                           * SH_CFLAGS -I list). */

extern void sourceboot_exception_illegal_instruction(void);
extern void sourceboot_exception_illegal_slot(void);
extern void sourceboot_exception_cpu_address_error(void);
extern void sourceboot_exception_dma_address_error(void);

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
#ifndef SATURN_DIAGNOSTIC_MODE
#define SATURN_DIAGNOSTIC_MODE 0
#endif
#ifndef SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
#define SATURN_FEATURE_COMPLETE_MARIO_ANIMATION 0
#endif

#define SOURCEBOOT_BOOT_TRACE_MAGIC 0x53394254U
#define SOURCEBOOT_BOOT_TRACE_VERSION 1U
#define SOURCEBOOT_CADENCE_TRACE_MAGIC 0x53394354U
#define SOURCEBOOT_CADENCE_TRACE_VERSION 2U

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

_Static_assert(sizeof(sm64_saturn_sourceboot_boot_trace_t) == 32U,
               "sourceboot boot trace ABI must remain eight words");

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence_begin;
    uint32_t observed_vblank_generation;
    uint32_t frame_generation;
    uint32_t build_generation;
    uint32_t presentation_generation;
    uint32_t dropped_vblank_credit;
    uint32_t simulation_vblank_crossings;
    uint32_t simulation_count;
    uint32_t construction_vblank_crossings;
    uint32_t construction_count;
    uint32_t transport_presentation_vblank_crossings;
    uint32_t transport_presentation_count;
    uint32_t slave_work_vblank_crossings;
    uint32_t slave_work_count;
    uint32_t master_finalize_vblank_crossings;
    uint32_t master_finalize_count;
    uint32_t sequence_end;
} sm64_saturn_sourceboot_cadence_trace_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint16_t last_id;
    uint16_t last_frame;
    uint16_t next_id;
    uint16_t seen_count;
    uint16_t reserved;
    uint32_t fallback_count;
    uint32_t corrupt_bounds_count;
    uint32_t result_hash;
    uint32_t seen_words[7];
} sm64_saturn_sourceboot_animation_sweep_t;

#define SOURCEBOOT_ANIMATION_SWEEP_MAGIC 0x53394153U
#define SOURCEBOOT_ANIMATION_SWEEP_VERSION 1U
_Static_assert(sizeof(sm64_saturn_sourceboot_animation_sweep_t) == 60U,
               "animation sweep ABI must remain fifteen words");
#if SATURN_DIAGNOSTIC_MODE == 1
volatile sm64_saturn_sourceboot_animation_sweep_t sourceboot_animation_sweep = {
    .magic = SOURCEBOOT_ANIMATION_SWEEP_MAGIC,
    .version = SOURCEBOOT_ANIMATION_SWEEP_VERSION,
};
#endif

_Static_assert(sizeof(sm64_saturn_sourceboot_cadence_trace_t) == 76U,
               "sourceboot cadence trace ABI must remain nineteen words");

enum {
    SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY = 1U,
    SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_CALLBACKS_REGISTERED,
    SOURCEBOOT_BOOT_TRACE_STAGE_MAIN_ENTRY,
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
/* Seed the ELF .data image so a correctly addressed trace read can prove the
 * program has not reached sourceboot's user_init() hook yet. The
 * cache-through writer below still publishes every runtime update to backing
 * WRAM. */
volatile sm64_saturn_sourceboot_boot_trace_t sourceboot_boot_trace = {
    .magic = SOURCEBOOT_BOOT_TRACE_MAGIC,
    .version = SOURCEBOOT_BOOT_TRACE_VERSION,
};

/* One fixed target-visible seqlock snapshot. The host pauses after each
 * VBlank and owns append-only edge history, avoiding a target-side ring in
 * scarce HWRAM while retaining a coherent cumulative counter sample. */
volatile sm64_saturn_sourceboot_cadence_trace_t sourceboot_cadence_trace = {
    .magic = SOURCEBOOT_CADENCE_TRACE_MAGIC,
    .version = SOURCEBOOT_CADENCE_TRACE_VERSION,
};

/* The Fast3D interpreter owns CPU-only matrix/vertex/resolve/profile state;
 * keep it in the NOLOAD LWRAM work arena rather than consuming HWRAM needed
 * by the VDP1 command banks.  It is explicitly initialized before the first
 * bootstrap profile read because .lwram_bss is not crt0-zeroed. */
static sm64_saturn_fast3d_frontend_t sourceboot_fast3d
    __attribute__((section(".lwram_bss"), used));
#define SOURCEBOOT_LWRAM_STATE \
    __attribute__((section(".lwram_bss"), used))
static uint32_t sourceboot_sim_ticks_accum SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_sim_tick_count SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_render_ticks_accum SOURCEBOOT_LWRAM_STATE;
static volatile uint32_t sourceboot_vblank_out_count __uncached;
static uint32_t sourceboot_sim_vblank_credit_dropped SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_bank_generation SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_bank_submitted SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_bank_displayed SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_bank_overwrite_attempts SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_bank_late_dma SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_dma_wait_ticks_accum SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_wait_ticks_accum SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_overwrite_wait_ticks_accum
    SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_transfer_faults SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_vdp1_transfer_queued_not_started
    SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_trace_scheduler_credit SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_trace_vdp1_presentation_generation
    SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_trace_vdp2_presentation_generation
    SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_simulation_vblank_crossings SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_simulation_count SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_transport_presentation_vblank_crossings
    SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_transport_presentation_count
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_render_overlap_phase_t sourceboot_render_overlap_phase
    __uncached;
static bool sourceboot_render_overlap_event_ok __uncached;
static sm64_saturn_frame_pipeline_t sourceboot_frame_pipeline
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_mario_actor_snapshot_t sourceboot_mario_snapshot
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_mario_actor_pose_t sourceboot_mario_pose
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_render_snapshot_bank_t sourceboot_render_snapshots
    SOURCEBOOT_LWRAM_STATE;
/* One NOLOAD actor owner: initialized explicitly through the P2 alias before
 * publication; do not restore standalone observer/bank storage. */
static sm64_saturn_actor_runtime_storage_t sourceboot_actor_runtime
    __attribute__((section(".lwram_actor_runtime"), used)) __aligned(16);
#define sourceboot_actor_observer sourceboot_actor_runtime.observer
#define sourceboot_actor_instances sourceboot_actor_runtime.instances
/* Each physical actor bank carries its own render-generation ticket.  A
 * later source tick may acquire the other bank while an earlier generation is
 * still rendering; no single global "active bank" may be overwritten. */
static uint32_t sourceboot_actor_bank_generation[2]
    SOURCEBOOT_LWRAM_STATE;
static const sm64_saturn_render_snapshot_t *sourceboot_active_render_snapshot
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp2_frame_t sourceboot_vdp2_frame
    SOURCEBOOT_LWRAM_STATE;
#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT
sm64_saturn_source_route_probe_t sourceboot_route_checkpoint;
#endif
sm64_saturn_camera_timing_t sm64_saturn_camera_timing SOURCEBOOT_LWRAM_STATE;
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
volatile sm64_saturn_math_route_capture_t sourceboot_math_route_capture
    SOURCEBOOT_LWRAM_STATE;
#endif

const sm64_saturn_input_replay_sample_t *
sm64_saturn_sourceboot_bob_parity_v1(uint16_t *sample_count);

static volatile sm64_saturn_sourceboot_boot_trace_t *
sourceboot_boot_trace_visible(void)
{
    /* Ymir's mem.peek and a hardware debugger observe backing WRAM, not
     * dirty SH-2 cache lines. Keep the ELF-visible P1 symbol above, but
     * publish every word through its P2 cache-through alias. */
    return (volatile sm64_saturn_sourceboot_boot_trace_t *)(
        CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_boot_trace);
}

static void sourceboot_boot_trace_write(uint32_t stage_id,
                                        uint32_t observed_vblank_generation)
{
    volatile sm64_saturn_sourceboot_boot_trace_t * const trace =
        sourceboot_boot_trace_visible();
    trace->magic = SOURCEBOOT_BOOT_TRACE_MAGIC;
    trace->version = SOURCEBOOT_BOOT_TRACE_VERSION;
    trace->observed_vblank_generation =
        observed_vblank_generation;
    trace->scheduler_credit =
        sourceboot_trace_scheduler_credit;
    trace->vdp1_presentation_generation =
        sourceboot_trace_vdp1_presentation_generation;
    trace->vdp2_presentation_generation =
        sourceboot_trace_vdp2_presentation_generation;
    trace->stage_id = stage_id;
    trace->stage++;
}

static volatile sm64_saturn_sourceboot_cadence_trace_t *
sourceboot_cadence_trace_visible(void)
{
    return (volatile sm64_saturn_sourceboot_cadence_trace_t *)(
        CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_cadence_trace);
}

static void sourceboot_phase_accumulate(uint32_t start, uint32_t end,
                                        uint32_t *crossings,
                                        uint32_t *count)
{
    *crossings += end - start;
    (*count)++;
}

static void sourceboot_cadence_trace_append(uint32_t frame_generation,
                                            uint32_t build_generation,
                                            uint32_t presentation_generation)
{
    volatile sm64_saturn_sourceboot_cadence_trace_t *const trace =
        sourceboot_cadence_trace_visible();
    const uint32_t next_sequence = (trace->sequence_end + 2U) & ~1U;
    trace->sequence_begin = next_sequence - 1U;
    trace->sequence_end = next_sequence - 1U;
    trace->observed_vblank_generation = sourceboot_vblank_out_count;
    trace->frame_generation = frame_generation;
    trace->build_generation = build_generation;
    trace->presentation_generation = presentation_generation;
    trace->dropped_vblank_credit = sourceboot_sim_vblank_credit_dropped;
    trace->simulation_vblank_crossings = sourceboot_simulation_vblank_crossings;
    trace->simulation_count = sourceboot_simulation_count;
    trace->construction_vblank_crossings =
        sourceboot_render_overlap_phase.construction_vblank_crossings;
    trace->construction_count =
        sourceboot_render_overlap_phase.construction_count;
    trace->transport_presentation_vblank_crossings =
        sourceboot_transport_presentation_vblank_crossings;
    trace->transport_presentation_count =
        sourceboot_transport_presentation_count;
    trace->slave_work_vblank_crossings =
        sourceboot_render_overlap_phase.slave_work_vblank_crossings;
    trace->slave_work_count =
        sourceboot_render_overlap_phase.slave_work_count;
    trace->master_finalize_vblank_crossings =
        sourceboot_render_overlap_phase.master_finalize_vblank_crossings;
    trace->master_finalize_count =
        sourceboot_render_overlap_phase.master_finalize_count;
    trace->sequence_end = next_sequence;
    /* Publish last: equality plus an even value identifies a stable sample. */
    trace->sequence_begin = next_sequence;
}

static uint16_t sourceboot_frt_delta(uint16_t start, uint16_t end)
{
    return (uint16_t)(end - start);
}

static int32_t sourceboot_world_to_q16(int32_t value)
{
    if (value > INT32_MAX / 65536) return INT32_MAX;
    if (value < INT32_MIN / 65536) return INT32_MIN;
    return value * 65536;
}

static void sourceboot_capture_render_snapshot(uint32_t generation)
{
    sm64_saturn_render_snapshot_t *snapshot = NULL;
    sm64_saturn_actor_capture_telemetry_t actor_stats;
    uint16_t actor_count = 0U;
    uint8_t actor_bank = 0xffU;
    uint32_t axis;

    if (!sm64_saturn_render_snapshot_begin_write(&sourceboot_render_snapshots,
                                                 generation, &snapshot)) {
        return;
    }
    /* The observer frame was opened before the authoritative source tick.
     * Capture only after that tick has completed; the observer contains
     * scalar geo decisions and never selects or mutates gameplay state. */
    sm64_saturn_geo_state_observer_end_frame(&sourceboot_actor_observer);
    if (sm64_saturn_actor_instance_bank_capture(
            &sourceboot_actor_instances, generation,
            SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE, &actor_bank, &actor_count,
            &actor_stats)) {
        uint16_t acquired_count = 0U;
        if (sm64_saturn_actor_instance_bank_acquire(
                &sourceboot_actor_instances, actor_bank, generation,
                &acquired_count) == NULL) {
            (void)sm64_saturn_actor_instance_bank_recycle_pre_acquire(
                &sourceboot_actor_instances, actor_bank, generation,
                SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
            actor_bank = 0xffU;
            actor_count = 0U;
        } else {
            actor_count = acquired_count;
            sourceboot_actor_bank_generation[actor_bank] = generation;
        }
    }
    snapshot->actor_instance_count = actor_count;
    snapshot->actor_instance_bank = actor_bank == 0xffU ? 0U : actor_bank;
    snapshot->actor_instance_bank_valid = actor_bank == 0xffU ? 0U : 1U;
    if (!sm64_saturn_mario_actor_snapshot(&snapshot->mario)) {
        snapshot->mario.valid = 0U;
    }
#if SATURN_DIAGNOSTIC_MODE == 1
    if (snapshot->mario.valid != 0U && sourceboot_animation_sweep.next_id < 209U) {
        snapshot->mario.animation_id = (int16_t)sourceboot_animation_sweep.next_id;
        snapshot->mario.animation_frame = 0;
        sourceboot_animation_sweep.last_id = sourceboot_animation_sweep.next_id++;
        sourceboot_animation_sweep.last_frame = 0U;
    }
#endif
    const uint8_t pose_ok = sm64_saturn_mario_actor_pose_selector(
        &snapshot->mario, &snapshot->mario_pose);
#if SATURN_DIAGNOSTIC_MODE == 1
    sm64_saturn_mario_actor_pose_t diagnostic_pose;
    if (pose_ok != 0U &&
        sm64_saturn_mario_actor_pose_from_selector(
            &snapshot->mario_pose, &diagnostic_pose) != 0U &&
        sourceboot_animation_sweep.last_id < 209U) {
        const uint16_t id = sourceboot_animation_sweep.last_id;
        const uint16_t word = (uint16_t)(id >> 5);
        const uint32_t mask = 1UL << (id & 31U);
        if ((sourceboot_animation_sweep.seen_words[word] & mask) == 0U) {
            sourceboot_animation_sweep.seen_words[word] |= mask;
            sourceboot_animation_sweep.seen_count++;
        }
        uint32_t hash = 2166136261UL;
        for (uint16_t vertex = 0U; vertex < diagnostic_pose.vertex_count; vertex++) {
            for (uint16_t axis = 0U; axis < 3U; axis++) {
                hash ^= (uint16_t)diagnostic_pose.vertices[vertex][axis];
                hash *= 16777619UL;
            }
        }
        sourceboot_animation_sweep.result_hash = hash;
    } else if (sourceboot_animation_sweep.next_id > 0U) {
        sourceboot_animation_sweep.fallback_count++;
    }
#endif
    for (axis = 0U; axis < 4U; axis++) {
        snapshot->camera.view_projection_q16[axis][axis] = 65536;
    }
    for (axis = 0U; axis < 3U; axis++) {
        snapshot->camera.camera_position_q16[axis] =
            sourceboot_world_to_q16(snapshot->mario.camera_position[axis]);
        snapshot->camera.camera_focus_q16[axis] =
            sourceboot_world_to_q16(snapshot->mario.camera_focus[axis]);
    }
    const sm64_saturn_vec3i_t forward = sm64_saturn_vec3_normalize_q16(
        (sm64_saturn_vec3i_t){
            snapshot->mario.camera_focus[0] - snapshot->mario.camera_position[0],
            snapshot->mario.camera_focus[1] - snapshot->mario.camera_position[1],
            snapshot->mario.camera_focus[2] - snapshot->mario.camera_position[2]});
    snapshot->camera.view_forward_q16[0] = forward.x;
    snapshot->camera.view_forward_q16[1] = forward.y;
    snapshot->camera.view_forward_q16[2] = forward.z;
    snapshot->camera.generation = generation;
    snapshot->actor_generation = generation;
    snapshot->scene_id = (uint32_t)gCurrLevelNum;
    snapshot->area_id = (uint32_t)gCurrAreaIndex;
    snapshot->geometry_bank_id = snapshot->mario_pose.vertex_bank_id;
    snapshot->material_bank_id = snapshot->mario_pose.material_bank_id;
    /* Must be written before publish() below flips this slot to READY --
     * acquire_ready() only trusts READY slots, so fields written after
     * publish() could be read torn/stale by a peer. */
    s8 power_meter_animation = 0;
    s16 power_meter_y = 0;
    get_hud_power_meter_state(&power_meter_animation, &power_meter_y);
    snapshot->hud.lives = gHudDisplay.lives;
    snapshot->hud.coins = gHudDisplay.coins;
    snapshot->hud.stars = gHudDisplay.stars;
    snapshot->hud.wedges = gHudDisplay.wedges;
    snapshot->hud.keys = gHudDisplay.keys;
    snapshot->hud.flags = gHudDisplay.flags;
    snapshot->hud.timer = gHudDisplay.timer;
    snapshot->hud.camera_status = get_hud_camera_status();
    snapshot->hud.power_meter_animation = power_meter_animation;
    snapshot->hud.power_meter_y = power_meter_y;
    snapshot->hud.cannon_active = (gCurrentArea != NULL &&
                                   gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON)
                                       ? 1U : 0U;
    snapshot->hud.reserved0 = 0U;
    if (!sm64_saturn_render_snapshot_publish(&sourceboot_render_snapshots,
                                             snapshot)) {
        (void)sm64_saturn_render_snapshot_quarantine(&sourceboot_render_snapshots,
                                                      generation);
        if (actor_bank != 0xffU &&
            sourceboot_actor_bank_generation[actor_bank] == generation) {
            (void)sm64_saturn_actor_instance_bank_quarantine(
                &sourceboot_actor_instances, generation);
            sourceboot_actor_bank_generation[actor_bank] = 0U;
        }
    }
}

static void sourceboot_run_source_tick(void)
{
    const uint16_t sim_start = cpu_frt_count_get();
    const uint32_t source_tick_generation =
        sm64_saturn_frame_pipeline_next_generation(sourceboot_sim_tick_count);
    /* Open the source-owned observation window before any game-loop geo walk.
     * The capture after this function must consume exactly this generation;
     * opening the frame in the capture routine would erase every object. */
    sm64_saturn_geo_state_observer_begin_frame(&sourceboot_actor_observer,
                                               source_tick_generation);
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
    sourceboot_sim_tick_count = source_tick_generation;
    sourceboot_fast3d.profile.sim_frt_ticks_accum =
        sourceboot_sim_ticks_accum;
    sourceboot_fast3d.profile.sim_tick_count = source_tick_generation;
    sourceboot_fast3d.profile.scene_graph_walks =
        sm64_saturn_source_runtime_state()->scene_graph_walks;
    sourceboot_fast3d.profile.scene_graph_walks_suppressed =
        sm64_saturn_source_runtime_state()->scene_graph_walks_suppressed;
    sourceboot_capture_render_snapshot(source_tick_generation);
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1 && !SATURN_SOURCEBOOT_LIVE_INPUT
    if (sm64_saturn_source_runtime_state()->input_replay_complete) {
        sm64_saturn_camera_bypass_arm(source_tick_generation);
    }
#endif
#if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1 && !SATURN_SOURCEBOOT_LIVE_INPUT
    sm64_saturn_sourceboot_camera_idle_probe_record(
        sm64_saturn_source_runtime_state(),
        source_tick_generation);
#endif
}

#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT
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
 * - Now: 0x5EC00 (379 KiB) in LWRAM, where ~1 MiB sits idle next to
 *   the 16 KiB VDP1 staging array. The 5 KiB reserved prefix is the
 *   permanent owner of the relocated route/frame/DMA state below; it keeps
 *   that state out of HWRAM without changing the level-pool contract.
 *   The old 0x30000 was itself too small
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
#define SOURCEBOOT_MAIN_POOL_BYTES (0x0005EC00UL)
static uint8_t sourceboot_main_pool[SOURCEBOOT_MAIN_POOL_BYTES]
    __attribute__((section(".lwram_bss"))) __aligned(16);

void *sm64_saturn_source_cart_phase_workspace(void)
{
    return sourceboot_main_pool;
}

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

/* HWRAM-resident command staging. Zeroed explicitly by
 * sm64_saturn_vdp1_backend_init_with_storage below, since this section
 * is not .bss and crt0 never visits it.
 *
 * Capacity raised 512 -> 2048 (2026-07-22) to track
 * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES's 192 -> 1536 increase 1:1
 * (saturn_fast3d_vdp1_emit.c emits exactly one vdp1_cmdt_t per resolved
 * triangle). Both 2048-command banks occupy 0x20000 bytes, aligned to the
 * VDP1 command layout. This ordinary HWRAM storage keeps the established
 * CPU-DMAC/VDP1 transfer addresses and cache behavior; do not reintroduce a
 * `.lwram_cmdts` attribute (the linker rejects that legacy section). */
static vdp1_cmdt_t sourceboot_vdp1_cmdts[2][SOURCEBOOT_VDP1_COMMAND_CAPACITY]
    __aligned(32);
static sm64_saturn_vdp1_backend_t sourceboot_vdp1_backend
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp1_frame_bank_set_t sourceboot_vdp1_frame_banks
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp1_transfer_targets_t sourceboot_vdp1_transfer_targets
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp1_frame_bank_t *sourceboot_active_build_bank
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp1_frame_bank_t *sourceboot_vdp1_render_ready
    SOURCEBOOT_LWRAM_STATE;
static sm64_saturn_vdp1_frame_bank_t *sourceboot_vdp1_transfer_pending
    SOURCEBOOT_LWRAM_STATE;
static bool sourceboot_vdp1_destination_poisoned
    SOURCEBOOT_LWRAM_STATE;
static bool sourceboot_render_started SOURCEBOOT_LWRAM_STATE;
static uint32_t sourceboot_failed_render_generation
    SOURCEBOOT_LWRAM_STATE;

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
 * a 0x1B00-byte floor at link time for libyaul's TLSF control block. */
static sm64_saturn_gouraud_table_t sourceboot_gouraud_staging[2]
    [SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
static sm64_saturn_gouraud_bank_t sourceboot_gouraud_banks[2]
    SOURCEBOOT_LWRAM_STATE;

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
 * entries in VDP2 VRAM and no work in the game/render loop.  The gradient
 * itself is pure integer arithmetic over a fixed boot asset (never camera
 * or simulation state), so it is baked to a build-time `static const` by
 * tools/saturn/gen_sourceboot_sky_gradient.py instead of computed into a
 * mutable HWRAM array every boot; the automatic *sm64-port*(.rodata) linker
 * rule (sourceboot-cart.x) places the constant on the 4 MiB DRAM cartridge
 * instead, recovering 448 bytes of HWRAM (2026-08-07 memory-budget audit,
 * Finding 2). See saturn_sky_gradient_generated.h for the baked table. */
#define SOURCEBOOT_BACKSCREEN_LINES 224U
_Static_assert(SOURCEBOOT_SKY_GRADIENT_GENERATED_LINES == SOURCEBOOT_BACKSCREEN_LINES,
    "generated sky gradient line count drifted from SOURCEBOOT_BACKSCREEN_LINES -- "
    "rerun tools/saturn/gen_sourceboot_sky_gradient.py or update both constants");

/* .lwram_bss is deliberately NOLOAD.  Keep the relocated CPU-only state
 * deterministic without asking crt0 to clear the whole LWRAM arena; the
 * transport arrays themselves are initialized by their existing owners below.
 * This reset is master-owned and runs before either frame pipeline or VDP1
 * bank can observe the state. */
static void sourceboot_reset_lwram_state(void)
{
    sourceboot_sim_ticks_accum = 0U;
    sourceboot_sim_tick_count = 0U;
    sourceboot_render_ticks_accum = 0U;
    sourceboot_sim_vblank_credit_dropped = 0U;
    sourceboot_vdp1_bank_generation = 0U;
    sourceboot_vdp1_bank_submitted = 0U;
    sourceboot_vdp1_bank_displayed = 0U;
    sourceboot_vdp1_bank_overwrite_attempts = 0U;
    sourceboot_vdp1_bank_late_dma = 0U;
    sourceboot_dma_wait_ticks_accum = 0U;
    sourceboot_vdp1_wait_ticks_accum = 0U;
    sourceboot_vdp1_overwrite_wait_ticks_accum = 0U;
    sourceboot_vdp1_transfer_faults = 0U;
    sourceboot_vdp1_transfer_queued_not_started = 0U;
    sourceboot_trace_scheduler_credit = 0U;
    sourceboot_trace_vdp1_presentation_generation = 0U;
    sourceboot_trace_vdp2_presentation_generation = 0U;
    sourceboot_simulation_vblank_crossings = 0U;
    sourceboot_simulation_count = 0U;
    sourceboot_transport_presentation_vblank_crossings = 0U;
    sourceboot_transport_presentation_count = 0U;
    sm64_saturn_camera_timing_reset();
    memset(&sourceboot_frame_pipeline, 0,
           sizeof(sourceboot_frame_pipeline));
    memset(&sourceboot_mario_snapshot, 0,
           sizeof(sourceboot_mario_snapshot));
    memset(&sourceboot_mario_pose, 0, sizeof(sourceboot_mario_pose));
    memset(&sourceboot_render_snapshots, 0,
           sizeof(sourceboot_render_snapshots));
    memset(sourceboot_actor_bank_generation, 0,
           sizeof(sourceboot_actor_bank_generation));
#if SATURN_SOURCEBOOT_ROUTE_REPLAY
    memset((void *)&sourceboot_math_route_capture, 0,
           sizeof(sourceboot_math_route_capture));
#endif
    sourceboot_active_render_snapshot = NULL;
    memset(&sourceboot_vdp2_frame, 0, sizeof(sourceboot_vdp2_frame));
    memset(&sourceboot_vdp1_backend, 0, sizeof(sourceboot_vdp1_backend));
    memset(&sourceboot_vdp1_frame_banks, 0,
           sizeof(sourceboot_vdp1_frame_banks));
    memset(&sourceboot_vdp1_transfer_targets, 0,
           sizeof(sourceboot_vdp1_transfer_targets));
    sourceboot_active_build_bank = NULL;
    sourceboot_vdp1_render_ready = NULL;
    sourceboot_vdp1_transfer_pending = NULL;
    sourceboot_vdp1_destination_poisoned = false;
    sourceboot_render_started = false;
    sourceboot_failed_render_generation = 0U;
    memset(sourceboot_gouraud_banks, 0,
           sizeof(sourceboot_gouraud_banks));
}

/* Remembers the layout last published to the VDP2 HUD atlas so
 * sm64_saturn_hud_publish() (called from sourceboot_present_generation())
 * can rewrite only the cells that changed. Ordinary .bss (crt0-zeroed), then
 * explicitly primed by sm64_saturn_hud_publish_init() below, alongside the
 * other one-writer frame-state globals in this file. */
static sm64_saturn_hud_publish_state_t sourceboot_hud_publish_state;

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
    /* Dark blue at the horizon, brighter blue overhead -- the table itself
     * (sourceboot_sky_gradient_generated) is baked at build time; see the
     * comment above SOURCEBOOT_BACKSCREEN_LINES. */
    vdp2_scrn_back_buffer_set(VDP2_VRAM_ADDR(3, 0x01FE00),
                              sourceboot_sky_gradient_generated,
                              SOURCEBOOT_SKY_GRADIENT_GENERATED_LINES);
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
    /* NBG1's sky bitmap is 512x256 @ RGB1555 = 0x40000 bytes -- exactly two
     * quarter-banks (0x20000 each), so it physically occupies BOTH bank A0
     * (.pt[0]) and bank A1 (.pt[1]) in full. All 8 of their combined slots
     * stay dedicated to NBG1, unreduced: an earlier draft of this carve-out
     * gave NBG0 one slot each in .pt[0]/.pt[1] by taking them from NBG1, but
     * that assumed the HUD atlas's character/pattern data lived in a bank
     * shared with the sky. It does not -- HUD_CPD_BASE/HUD_PND_BASE
     * (saturn_hud_atlas.c) were relocated to bank B0 (VDP2_VRAM_ADDR(2, ..))
     * specifically because it is unclaimed by anything else in this target,
     * so NBG0 gets its own slots there (.pt[2]) instead of contending with
     * NBG1 for A0/A1 bandwidth. One PNDR slot is always sufficient regardless
     * of color depth; CHPNDR slot count scales with color depth, and NBG0 is
     * RGB_32768 like NBG1, so it gets the same 4-slot provision NBG1 uses
     * per bank -- bank B0 has 8 total and nothing else competes for it.
     *
     * NBG0's CHPNDR slots are t1,t2,t4,t5 -- deliberately skipping t3.
     * The VDP2 cycle-pattern timing rule couples the two access kinds: with
     * the pattern-name read at T0, character-pattern reads are only legal in
     * the slots the T0 PND fetch can feed, and T3 is excluded from that set
     * (Ymir's kLoResPatterns table mirrors the same T0-PND/T3-CPD exclusion
     * and happens to render it leniently; real hardware does not). An
     * earlier revision used t1-t4, which drops NBG0's character fetch on
     * hardware even though every emulator frame looked fine.
     *
     * Bank B0's leftover slots (t3,t6,t7) are explicit NO_ACCESS because a
     * designated-initializer zero is NOT "no access" in this encoding:
     * VDP2_VRAM_CYCP_PNDR_NBG0 is 0x0 (vram.h) and NO_ACCESS is 0xF, so an
     * unset slot in the one bank that really holds NBG0's PND would silently
     * grant an extra NBG0 pattern-name slot -- including at T3, which could
     * move the PND fetch off T0 and void the CPD-slot legality above. The
     * other banks' unset slots also decay to PNDR_NBG0, but NBG0 has no data
     * there for the grant to serve (pre-existing, unchanged here). */
    const vdp2_vram_cycp_t cycles = {
        .pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[2].t0 = VDP2_VRAM_CYCP_PNDR_NBG0,
        .pt[2].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t3 = VDP2_VRAM_CYCP_NO_ACCESS,
        .pt[2].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t5 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t6 = VDP2_VRAM_CYCP_NO_ACCESS,
        .pt[2].t7 = VDP2_VRAM_CYCP_NO_ACCESS,
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
    /* NBG1 is an opaque baked sky and must remain behind VDP1. NBG0 hosts
     * the gameplay HUD and stays above everything, including dbgio's NBG3
     * diagnostics text. Every sprite group stays visible above the sky.
     *
     * Sprites are capped one level BELOW NBG0's 7: VDP2 resolves an
     * equal-priority tie in the sprite layer's favor, and VDP1's frame
     * covers the whole raster, so sprites at 7 buried every HUD cell under
     * terrain -- target-proven in the Task 9 investigation (corrected HUD
     * cells rendered over sprite-free regions but never over VDP1 pixels).
     * The caller's requested vdp1_priority is honored up to that cap; NBG0
     * alone owns level 7. */
    const uint8_t sprite_priority = (vdp1_priority > 6U) ? 6U : vdp1_priority;
    for (uint8_t priority = 0U; priority < 8U; priority++)
        vdp2_sprite_priority_set(priority, sprite_priority);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 0U);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7U); /* gameplay HUD: always on top, matching Z-Treme's NBG3 font-plane precedent */
    vdp2_scrn_priority_set(VDP2_SCRN_NBG3, 6U); /* dbgio diagnostics: below the HUD */
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
sourceboot_vdp2_camera_snapshot(
    const sm64_saturn_vdp1_frame_bank_t *bank)
{
    return bank->camera_snapshot;
}

/* The master alone converts one observed VBlank generation into one VDP1
 * plot and one VDP2 composition commit.  No geometry enters the VDP2 API;
 * its frame is prepared only after VDP1's terminal completion boundary. */
static void sourceboot_present_generation(
    const sm64_saturn_vdp1_frame_bank_t *bank)
{
    const uint32_t presentation_generation = bank->snapshot_generation;
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
    sourceboot_fast3d.profile.vdp1_wait_ticks_last = 0U;
    sourceboot_fast3d.profile.vdp1_wait_ticks_accum =
        sourceboot_vdp1_wait_ticks_accum;
    sourceboot_fast3d.profile.vdp1_terminal_fence_wait_ticks_last =
        0U;
    sourceboot_fast3d.profile.vdp1_terminal_fence_wait_ticks_accum =
        0U;
    const sm64_saturn_vdp2_camera_snapshot_t vdp2_camera =
        sourceboot_vdp2_camera_snapshot(bank);
    sm64_saturn_hud_publish(&sourceboot_hud_publish_state, &bank->hud);
    const sm64_saturn_vdp2_generation_state_t vdp2_generations = {
        .displayed_generation = presentation_generation,
        .rendered_generation = presentation_generation,
        .simulation_generation = sourceboot_frame_pipeline.simulation_generation,
    };
    sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, &vdp2_camera,
                                 &sourceboot_fast3d.profile,
                                 &vdp2_generations,
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
    sourceboot_fast3d.profile.vblank_presentation_generation =
        presentation_generation;
    sourceboot_fast3d.profile.sim_vblank_credit_dropped =
        sourceboot_sim_vblank_credit_dropped;
}

static void sourceboot_frame_run_sim_tick(uint32_t generation)
{
    const uint32_t simulation_vblank_start = sourceboot_vblank_out_count;
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_BEFORE,
                                generation);
    sourceboot_run_source_tick();
    if (sourceboot_sim_tick_count != generation)
        sourceboot_fast3d.profile.pipeline_faults++;
    sourceboot_phase_accumulate(simulation_vblank_start,
                                sourceboot_vblank_out_count,
                                &sourceboot_simulation_vblank_crossings,
                                &sourceboot_simulation_count);
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_SOURCE_TICK_AFTER,
                                generation);
}

#if SATURN_DEMO_PATH
static uint32_t sourceboot_render_marker_clock(void *context)
{
    (void)context;
    return sourceboot_vblank_out_count;
}

static void sourceboot_render_runtime_marker(
    void *context, sm64_saturn_render_job_runtime_marker_t marker,
    uint32_t generation, uint32_t sequence, uint32_t marker_vblank)
{
    (void)context;
    (void)sequence;
    bool accepted = false;
    if (marker == SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_NOTIFIED)
        accepted = sm64_saturn_render_overlap_phase_notification_published(
            &sourceboot_render_overlap_phase, generation, marker_vblank);
    else if (marker == SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_RETIRED)
        accepted = sm64_saturn_render_overlap_phase_retirement_published(
            &sourceboot_render_overlap_phase, generation, marker_vblank);
    sourceboot_render_overlap_event_ok =
        sourceboot_render_overlap_event_ok && accepted;
}

static bool sourceboot_render_overlap_terminal(uint32_t generation)
{
    if (!sourceboot_render_overlap_phase.active) return true;
    if (!sourceboot_render_overlap_phase.notified)
        return sm64_saturn_render_overlap_phase_abort(
            &sourceboot_render_overlap_phase, generation,
            sourceboot_vblank_out_count);
    if (sourceboot_active_render_snapshot == NULL ||
        sourceboot_active_build_bank == NULL)
        return false;
    return sourceboot_render_overlap_event_ok &&
        sm64_saturn_render_overlap_phase_terminal(
            &sourceboot_render_overlap_phase, generation,
            sourceboot_active_render_snapshot,
            sourceboot_active_build_bank, sourceboot_vblank_out_count);
}
#endif

static void sourceboot_frame_service_render(uint32_t generation)
{
    const uint16_t render_start = cpu_frt_count_get();
#if SATURN_DEMO_PATH
    sm64_saturn_demo_render_status_t render_status =
        SM64_SATURN_DEMO_RENDER_PENDING;
#endif
    bool render_complete = false;

    if (sourceboot_failed_render_generation == generation) goto finish;

    if (sourceboot_active_render_snapshot != NULL) {
        if (!sourceboot_render_started || sourceboot_active_build_bank == NULL ||
            sourceboot_active_render_snapshot->generation != generation ||
            sourceboot_active_build_bank->snapshot_generation != generation ||
            sourceboot_active_build_bank->state !=
                SM64_SATURN_VDP1_FRAME_BANK_BUILDING) {
            goto failed;
        }
#if SATURN_DEMO_PATH
        if (!sm64_saturn_render_overlap_phase_retains(
                &sourceboot_render_overlap_phase, generation,
                sourceboot_active_render_snapshot,
                sourceboot_active_build_bank))
            goto failed;
        render_status = sm64_saturn_demo_render_poll_frame(
            &sourceboot_fast3d.profile, generation);
        if (render_status == SM64_SATURN_DEMO_RENDER_PENDING) goto finish;
        if (!sourceboot_render_overlap_terminal(generation)) goto failed;
        if (render_status == SM64_SATURN_DEMO_RENDER_FAILED) goto failed;
        if (render_status != SM64_SATURN_DEMO_RENDER_COMPLETE) goto failed;
        render_complete = true;
#else
        goto failed;
#endif
    } else {
        if (sourceboot_render_started) goto failed;
#if SATURN_DEMO_PATH
        sourceboot_render_overlap_event_ok = true;
        if (!sm64_saturn_render_overlap_phase_begin(
                &sourceboot_render_overlap_phase, generation,
                sourceboot_vblank_out_count))
            goto failed;
#endif

        sourceboot_active_render_snapshot =
            sm64_saturn_render_snapshot_acquire_ready(
                &sourceboot_render_snapshots, generation);
        if (sourceboot_active_render_snapshot == NULL ||
            sourceboot_active_render_snapshot->generation != generation)
            goto failed;

        sourceboot_mario_snapshot = sourceboot_active_render_snapshot->mario;
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
        (void)sm64_saturn_mario_actor_pose_from_selector(
            &sourceboot_active_render_snapshot->mario_pose,
            &sourceboot_mario_pose);
#else
        (void)sm64_saturn_mario_actor_pose(&sourceboot_mario_snapshot,
                                           &sourceboot_mario_pose);
#endif
        sourceboot_fast3d.profile.demo_actor_snapshot_valid =
            sourceboot_mario_snapshot.valid;
        sourceboot_fast3d.profile.demo_actor_pose_vertices =
            sourceboot_mario_pose.vertex_count;

        if (vdp1_sync_busy()) sourceboot_vdp1_bank_late_dma++;
        render_complete = sm64_saturn_vdp1_frame_bank_begin_build(
            &sourceboot_vdp1_frame_banks, generation,
            &sourceboot_active_build_bank);
        if (!render_complete) {
            sourceboot_vdp1_bank_overwrite_attempts++;
            goto failed;
        }
#if SATURN_DEMO_PATH
        if (!sm64_saturn_render_overlap_phase_bind(
                &sourceboot_render_overlap_phase, generation,
                sourceboot_active_render_snapshot,
                sourceboot_active_build_bank))
            goto failed;
#endif

        const sm64_saturn_vdp2_camera_snapshot_t camera_snapshot = {
            .yaw = sourceboot_mario_snapshot.camera_yaw,
            .pitch = sourceboot_mario_snapshot.camera_pitch,
            .valid = sourceboot_mario_snapshot.valid,
            .generation = generation,
        };
        render_complete = sm64_saturn_vdp1_frame_bank_set_camera_snapshot(
            sourceboot_active_build_bank, &camera_snapshot);
        if (!render_complete) goto failed;
        render_complete = sm64_saturn_vdp1_frame_bank_set_hud_snapshot(
            sourceboot_active_build_bank,
            &sourceboot_active_render_snapshot->hud);
        if (!render_complete) goto failed;
        sm64_saturn_vdp1_backend_bind_frame_bank(&sourceboot_vdp1_backend,
                                                 sourceboot_active_build_bank);
#if SATURN_DEMO_PATH
        render_complete = sm64_saturn_demo_render_start_frame(
            &sourceboot_vdp1_backend,
            sourceboot_active_build_bank->gouraud_bank,
            &sourceboot_fast3d.profile, &sourceboot_mario_snapshot,
            &sourceboot_mario_pose, generation);
        if (!render_complete) goto failed;
        sourceboot_render_started = true;
        goto finish;
#else
        render_complete = sm64_saturn_fast3d_vdp1_emit(
            &sourceboot_fast3d, &sourceboot_vdp1_backend,
            sourceboot_active_build_bank->gouraud_bank);
        if (!render_complete) goto failed;
#endif
    }

    if (render_complete) {
        render_complete = sm64_saturn_vdp1_frame_bank_ready(
            sourceboot_active_build_bank,
            sourceboot_vdp1_backend.list.count,
            sourceboot_active_build_bank->gouraud_bank->used, generation);
    }
    if (render_complete) {
        sourceboot_vdp1_bank_generation = generation;
        sourceboot_vdp1_render_ready = sourceboot_active_build_bank;
        render_complete = sm64_saturn_frame_pipeline_render_complete(
            &sourceboot_frame_pipeline, generation);
    }
    if (!render_complete) goto failed;

    if (!sm64_saturn_render_snapshot_complete(
            &sourceboot_render_snapshots, sourceboot_active_render_snapshot) ||
        !sm64_saturn_render_snapshot_retire(
            &sourceboot_render_snapshots, sourceboot_active_render_snapshot)) {
        sourceboot_fast3d.profile.pipeline_faults++;
    }
    if (sourceboot_active_render_snapshot->actor_instance_bank_valid != 0U) {
        const uint8_t actor_bank =
            sourceboot_active_render_snapshot->actor_instance_bank;
        if (actor_bank >= 2U ||
            sourceboot_actor_bank_generation[actor_bank] != generation ||
            !sm64_saturn_actor_instance_bank_complete(
                &sourceboot_actor_instances, actor_bank) ||
            !sm64_saturn_actor_instance_bank_retire(
                &sourceboot_actor_instances, actor_bank)) {
            if (actor_bank < 2U &&
                sourceboot_actor_bank_generation[actor_bank] == generation)
                (void)sm64_saturn_actor_instance_bank_quarantine(
                    &sourceboot_actor_instances, generation);
            sourceboot_fast3d.profile.pipeline_faults++;
        }
        if (actor_bank < 2U &&
            sourceboot_actor_bank_generation[actor_bank] == generation)
            sourceboot_actor_bank_generation[actor_bank] = 0U;
    }
    sourceboot_active_render_snapshot = NULL;
    sourceboot_active_build_bank = NULL;
    sourceboot_render_started = false;
    goto finish;

failed:
#if SATURN_DEMO_PATH
    if (!sourceboot_render_overlap_terminal(generation))
        sourceboot_fast3d.profile.pipeline_faults++;
#endif
    if (sourceboot_active_build_bank != NULL &&
        sourceboot_active_build_bank->state !=
            SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED)
        (void)sm64_saturn_vdp1_frame_bank_quarantine(
            sourceboot_active_build_bank);
    (void)sm64_saturn_render_snapshot_quarantine(
        &sourceboot_render_snapshots, generation);
    if (sourceboot_active_render_snapshot != NULL &&
        sourceboot_active_render_snapshot->actor_instance_bank_valid != 0U) {
        const uint8_t actor_bank =
            sourceboot_active_render_snapshot->actor_instance_bank;
        if (actor_bank < 2U &&
            sourceboot_actor_bank_generation[actor_bank] == generation) {
            (void)sm64_saturn_actor_instance_bank_quarantine(
                &sourceboot_actor_instances, generation);
            sourceboot_actor_bank_generation[actor_bank] = 0U;
        }
    }
    sourceboot_active_render_snapshot = NULL;
    sourceboot_active_build_bank = NULL;
    sourceboot_vdp1_render_ready = NULL;
    sourceboot_render_started = false;
    sourceboot_failed_render_generation = generation;
    sourceboot_fast3d.profile.pipeline_faults++;

finish:
    sourceboot_fast3d.profile.render_frt_ticks_last =
        sourceboot_frt_delta(render_start, cpu_frt_count_get());
    sourceboot_render_ticks_accum +=
        sourceboot_fast3d.profile.render_frt_ticks_last;
    sourceboot_fast3d.profile.render_frt_ticks_accum =
        sourceboot_render_ticks_accum;
}

static void sourceboot_frame_poll_transfers(uint32_t generation)
{
    const uint32_t transport_vblank_start = sourceboot_vblank_out_count;
    sm64_saturn_vdp1_frame_bank_t *bank = sourceboot_vdp1_transfer_pending;

    if (bank == NULL) {
        bank = sourceboot_vdp1_render_ready;
        if (bank == NULL || bank->snapshot_generation != generation) {
            sourceboot_fast3d.profile.pipeline_faults++;
            goto finish;
        }
        const bool overwrite_waited = vdp1_sync_busy();
        const uint16_t overwrite_wait_start = cpu_frt_count_get();
        if (overwrite_waited) vdp1_sync_wait();
        sourceboot_fast3d.profile.vdp1_overwrite_wait_ticks_last =
            overwrite_waited
                ? sourceboot_frt_delta(overwrite_wait_start,
                                       cpu_frt_count_get())
                : 0U;
        sourceboot_vdp1_overwrite_wait_ticks_accum +=
            sourceboot_fast3d.profile.vdp1_overwrite_wait_ticks_last;
        sourceboot_fast3d.profile.vdp1_overwrite_wait_ticks_accum =
            sourceboot_vdp1_overwrite_wait_ticks_accum;
        if (!sm64_saturn_vdp1_frame_bank_submit_transfers(
                bank, &sourceboot_vdp1_transfer_targets)) {
            /* Submission can fail after queue ownership changes. Treat the
             * shared resident destination as potentially partially written. */
            sourceboot_vdp1_destination_poisoned = true;
            (void)sm64_saturn_vdp1_frame_bank_quarantine(bank);
            sourceboot_vdp1_render_ready = NULL;
            sourceboot_fast3d.profile.pipeline_faults++;
            sourceboot_vdp1_transfer_faults++;
            goto finish;
        }
        sourceboot_vdp1_transfer_pending = bank;
        sourceboot_vdp1_render_ready = NULL;
        sourceboot_vdp1_bank_submitted = generation;
        if (!saturn_dma_queue_sequence_started(bank->command_transfer_ticket) &&
            !saturn_dma_queue_sequence_retired(bank->command_transfer_ticket))
            sourceboot_vdp1_transfer_queued_not_started++;
    }

    if (bank->snapshot_generation != generation) {
        sourceboot_vdp1_destination_poisoned = true;
        (void)sm64_saturn_vdp1_frame_bank_quarantine(bank);
        sourceboot_vdp1_transfer_pending = NULL;
        sourceboot_vdp1_transfer_faults++;
        sourceboot_fast3d.profile.pipeline_faults++;
        goto finish;
    }
    if (sm64_saturn_vdp1_frame_bank_poll_transfers(bank)) {
        if (!sm64_saturn_frame_pipeline_transfer_complete(
                &sourceboot_frame_pipeline, generation))
            sourceboot_fast3d.profile.pipeline_faults++;
    } else if (bank->state == SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED) {
        sourceboot_fast3d.profile.pipeline_faults++;
        sourceboot_vdp1_transfer_faults++;
        sourceboot_vdp1_destination_poisoned = true;
        sourceboot_vdp1_transfer_pending = NULL;
    }

finish:
    sourceboot_phase_accumulate(transport_vblank_start,
                                sourceboot_vblank_out_count,
                                &sourceboot_transport_presentation_vblank_crossings,
                                &sourceboot_transport_presentation_count);
}

static void sourceboot_frame_update_telemetry(void);

static void sourceboot_frame_publish(uint32_t generation)
{
    const uint32_t transport_vblank_start = sourceboot_vblank_out_count;
    sm64_saturn_vdp1_frame_bank_t *const bank =
        sourceboot_vdp1_transfer_pending;
    sm64_saturn_vdp1_frame_bank_t *const previous =
        sourceboot_vdp1_frame_banks.published;
    bool published = bank != NULL &&
        bank->snapshot_generation == generation &&
        !sourceboot_vdp1_destination_poisoned;

    if (published)
        published = sm64_saturn_vdp1_frame_bank_arm_resident_list(bank);
    if (published) {
        vdp1_sync_force_put();
        published = sm64_saturn_vdp1_frame_bank_publish(
            &sourceboot_vdp1_frame_banks, bank);
    }
    if (published) {
        sourceboot_vdp1_transfer_pending = NULL;
        if (previous != NULL &&
            !sm64_saturn_vdp1_frame_bank_retire(
                &sourceboot_vdp1_frame_banks,
                previous->snapshot_generation))
            sourceboot_fast3d.profile.pipeline_faults++;
    } else {
        if (bank != NULL) {
            (void)sm64_saturn_vdp1_frame_bank_quarantine(bank);
            sourceboot_vdp1_transfer_pending = NULL;
        }
        sourceboot_vdp1_destination_poisoned = true;
        sourceboot_vdp1_transfer_faults++;
        sourceboot_fast3d.profile.pipeline_faults++;
    }
    const bool publish_acknowledged =
        sm64_saturn_frame_pipeline_publish_complete(
            &sourceboot_frame_pipeline, generation, published);
    if (!publish_acknowledged) {
        if (published) sourceboot_vdp1_destination_poisoned = true;
        sourceboot_fast3d.profile.pipeline_faults++;
    }
    if (published && publish_acknowledged)
        sourceboot_vdp1_bank_displayed = generation;
    sourceboot_frame_update_telemetry();
    if (published && publish_acknowledged) {
        sourceboot_present_generation(bank);
        sourceboot_cadence_trace_append(sourceboot_sim_tick_count,
                                        sourceboot_vdp1_bank_generation,
                                        generation);
    }
    sourceboot_phase_accumulate(transport_vblank_start,
                                sourceboot_vblank_out_count,
                                &sourceboot_transport_presentation_vblank_crossings,
                                &sourceboot_transport_presentation_count);
}

static void sourceboot_frame_reuse_previous(uint32_t generation)
{
    if (sourceboot_vdp1_frame_banks.published != NULL &&
        !sourceboot_vdp1_destination_poisoned &&
        sourceboot_vdp1_frame_banks.published->snapshot_generation == generation) {
        sourceboot_present_generation(sourceboot_vdp1_frame_banks.published);
    }
}

static void sourceboot_frame_update_telemetry(void)
{
    sourceboot_sim_vblank_credit_dropped =
        sourceboot_frame_pipeline.dropped_sim_tick_credits;
    sourceboot_trace_scheduler_credit =
        sourceboot_frame_pipeline.available_sim_credit;
    sourceboot_fast3d.profile.sim_vblank_credit_dropped =
        sourceboot_sim_vblank_credit_dropped;
    sourceboot_fast3d.profile.vdp1_commands_last =
        sourceboot_vdp1_frame_banks.published != NULL
            ? sourceboot_vdp1_frame_banks.published->command_count : 0U;
    sourceboot_fast3d.profile.vdp1_commands =
        sourceboot_fast3d.profile.vdp1_commands_last;
    sourceboot_fast3d.profile.vdp1_bank_generation =
        sourceboot_vdp1_bank_generation;
    sourceboot_fast3d.profile.vdp1_bank_submitted =
        sourceboot_vdp1_bank_submitted;
    sourceboot_fast3d.profile.vdp1_bank_displayed =
        sourceboot_vdp1_bank_displayed;
    sourceboot_fast3d.profile.vdp1_bank_overwrite_attempts =
        sourceboot_vdp1_bank_overwrite_attempts;
    sourceboot_fast3d.profile.vdp1_bank_unavailable_skips =
        sourceboot_vdp1_bank_overwrite_attempts;
    sourceboot_fast3d.profile.vdp1_transfer_faults =
        sourceboot_vdp1_transfer_faults;
    sourceboot_fast3d.profile.vdp1_transfer_queued_not_started =
        sourceboot_vdp1_transfer_queued_not_started;
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
    sourceboot_fast3d.profile.command_cpu_dmac_wait_ticks_last = 0U;
    sourceboot_fast3d.profile.command_cpu_dmac_wait_ticks_accum = 0U;
    sourceboot_fast3d.profile.gouraud_scu_dma_wait_ticks_last = 0U;
    sourceboot_fast3d.profile.gouraud_scu_dma_wait_ticks_accum = 0U;
    if (sourceboot_fast3d.profile.vdp1_commands_last >
        sourceboot_fast3d.profile.vdp1_command_highwater)
        sourceboot_fast3d.profile.vdp1_command_highwater =
            sourceboot_fast3d.profile.vdp1_commands_last;
    const uint32_t gouraud_highwater =
        sourceboot_vdp1_frame_banks.published != NULL
            ? sourceboot_vdp1_frame_banks.published->gouraud_count : 0U;
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
    sourceboot_fast3d.profile.vdp2_vram_bytes = SOURCEBOOT_VDP2_VRAM_BYTES;
#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT
    sourceboot_capture_route_checkpoint();
#endif
}

static void sourceboot_frame_pipeline_dispatch(
    sm64_saturn_frame_action_t action, uint32_t generation)
{
    /* step() has already consumed this observation. Publish scheduler-owned
     * credit before action traces/cadence samples, then refresh all profile
     * mirrors again after the action mutates runtime state. */
    sourceboot_sim_vblank_credit_dropped =
        sourceboot_frame_pipeline.dropped_sim_tick_credits;
    sourceboot_trace_scheduler_credit =
        sourceboot_frame_pipeline.available_sim_credit;
    switch (action) {
        case SM64_SATURN_FRAME_RUN_SIM_TICK:
            sourceboot_frame_run_sim_tick(generation);
            break;
        case SM64_SATURN_FRAME_SERVICE_RENDER_JOBS:
            sourceboot_frame_service_render(generation);
            break;
        case SM64_SATURN_FRAME_POLL_TRANSFERS:
            sourceboot_frame_poll_transfers(generation);
            break;
        case SM64_SATURN_FRAME_PUBLISH_FRAME:
            sourceboot_frame_publish(generation);
            break;
        case SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME:
            sourceboot_frame_reuse_previous(generation);
            break;
        case SM64_SATURN_FRAME_WAIT_VBLANK:
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_BEFORE, generation);
            sm64_saturn_source_runtime_wait_vblank();
            sourceboot_boot_trace_write(
                SOURCEBOOT_BOOT_TRACE_STAGE_STALE_WAIT_AFTER, generation);
            break;
    }
    sourceboot_frame_update_telemetry();
}

void user_init(void) {
    /* First, matching both siblings' user_init order (castleviewer
     * main.c:1186, marioturntable main.c:247). */
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_ENTRY,
                                0U);
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
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_USER_INIT_CALLBACKS_REGISTERED, 0U);
    smpc_peripheral_intback_issue();
}

int main(void) {
    /* Install the project-owned exception trampolines immediately after
     * Yaul's crt0/__cpu_init path, before cart or scene setup can fault.  The
     * trampolines preserve the original frame and delegate to Yaul's normal
     * reset/debug-screen handler after recording it. */
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_INSTRUCTION,
                     sourceboot_exception_illegal_instruction);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_SLOT,
                     sourceboot_exception_illegal_slot);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_CPU_ADDRESS_ERROR,
                     sourceboot_exception_cpu_address_error);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_DMA_ADDRESS_ERROR,
                     sourceboot_exception_dma_address_error);
    /* The scene worker can fault independently of the master.  Yaul exposes
     * the slave table as the same vector IDs plus its base; install the same
     * frame-preserving trampolines there so a worker-side reset cannot erase
     * the evidence before the master observes it. */
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_INSTRUCTION +
                         CPU_INTC_INTERRUPT_SLAVE_BASE,
                     sourceboot_exception_illegal_instruction);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_SLOT +
                         CPU_INTC_INTERRUPT_SLAVE_BASE,
                     sourceboot_exception_illegal_slot);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_CPU_ADDRESS_ERROR +
                         CPU_INTC_INTERRUPT_SLAVE_BASE,
                     sourceboot_exception_cpu_address_error);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_DMA_ADDRESS_ERROR +
                         CPU_INTC_INTERRUPT_SLAVE_BASE,
                     sourceboot_exception_dma_address_error);
    /* Keep the one-shot SH-2 kernel vector observable in headless Ymir's
     * no-cart negative-control configuration too: cart loading may fail
     * before the source game loop is available. */
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_MAIN_ENTRY, 0U);
    if (!sm64_saturn_build_identity_is_valid(&saturn_build_identity)) {
        for (;;) {}
    }
    sm64_saturn_sourceboot_q16_kernel_probe_run();
    const sm64_saturn_source_cart_status_t cart_status =
        sm64_saturn_source_cart_load();
    if (cart_status != SM64_SATURN_SOURCE_CART_OK) {
        sm64_saturn_source_cart_report_failure(cart_status);
        for (;;) {}
    }
    {
        sm64_saturn_scene_package_view_t scene_package_view;
        const sm64_saturn_source_cart_status_t scene_status =
            sm64_saturn_source_cart_boot_scene_package_validate(
                &scene_package_view);
        if (scene_status != SM64_SATURN_SOURCE_CART_OK) {
            sm64_saturn_source_cart_report_failure(scene_status);
            for (;;) {}
        }
    }
    /* The bitmap is linked in .cart_rodata and is not readable from its
     * final DRAM-cart address until source_cart_load() has completed. Keep
     * the VDP2 format setup in user_init(), but defer the actual copy so NBG1
     * never receives a zeroed pre-cart buffer. */
    sourceboot_reset_lwram_state();
    sourceboot_init_sky_bitmap();
    sm64_saturn_hud_atlas_init();
    sm64_saturn_hud_publish_init(&sourceboot_hud_publish_state);
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
    sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);
    sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, NULL,
                                 &sourceboot_fast3d.profile,
                                 NULL,
                                 sourceboot_sim_tick_count);
    sm64_saturn_vdp2_frame_commit(&sourceboot_vdp2_frame,
                                  &sourceboot_vdp2_backend);
    vdp2_sync_wait();
    sourceboot_boot_trace_write(
        SOURCEBOOT_BOOT_TRACE_STAGE_BOOTSTRAP_RETIRED, 0U);
    sm64_saturn_render_snapshot_reset(&sourceboot_render_snapshots);
    memset((void *)(CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_actor_runtime),
           0, sizeof(sourceboot_actor_runtime));
    sm64_saturn_geo_state_observer_init(
        &sourceboot_actor_observer, SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE);
    sm64_saturn_actor_instance_bank_init(&sourceboot_actor_instances);
    sm64_saturn_actor_instances_set_observer(&sourceboot_actor_observer);
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
        /* Both scene paths can acquire either source bank. Initialize bank 1's
         * fixed system/local/END prefix unconditionally; binding storage does
         * not synthesize those commands. */
        sm64_saturn_vdp1_backend_t spare_backend;
        if (!sm64_saturn_vdp1_backend_init_with_storage(
                &spare_backend, sourceboot_vdp1_cmdts[1],
                SOURCEBOOT_VDP1_COMMAND_CAPACITY, clip, local)) {
            dbgio_puts("sourceboot: spare VDP1 backend init failed\n");
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

        vdp1_vram_partitions_get(&partitions);
#if SATURN_DEMO_PATH
        sm64_saturn_render_overlap_phase_init(
            &sourceboot_render_overlap_phase);
        sm64_saturn_demo_render_init();
        if (!sm64_saturn_render_job_runtime_observe_markers(
                sourceboot_render_runtime_marker,
                sourceboot_render_marker_clock, NULL)) {
            dbgio_puts("sourceboot: render marker observer failed\n");
            for (;;) {}
        }
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
        if (!sm64_saturn_vdp1_frame_bank_set_init(
                &sourceboot_vdp1_frame_banks,
                sourceboot_vdp1_cmdts[0], sourceboot_vdp1_cmdts[1],
                SOURCEBOOT_VDP1_COMMAND_CAPACITY,
                &sourceboot_gouraud_banks[0],
                &sourceboot_gouraud_banks[1])) {
            dbgio_puts("sourceboot: VDP1 frame-bank init failed\n");
            dbgio_flush();
            for (;;) {}
        }
        sourceboot_vdp1_transfer_targets =
            (sm64_saturn_vdp1_transfer_targets_t){
                .command_vram = (void *)(uintptr_t)VDP1_VRAM(0),
                .gouraud_vram = partitions.gouraud_base,
                .command_capacity_bytes =
                    SOURCEBOOT_VDP1_COMMAND_CAPACITY * sizeof(vdp1_cmdt_t),
                .gouraud_capacity_bytes =
                    (size_t)capacity * sizeof(sm64_saturn_gouraud_table_t),
            };
        /* Exclusive frame-transport handoff: every boot-time SCU/CPU DMA
         * above is complete before the serial queue owns both channel 0s. */
        saturn_dma_queue_init();
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
    sm64_saturn_frame_pipeline_init(&sourceboot_frame_pipeline,
                                     sourceboot_vblank_out_count, 0U);
    sourceboot_boot_trace_write(SOURCEBOOT_BOOT_TRACE_STAGE_THREAD5_AFTER,
                                sourceboot_vblank_out_count);
    for (;;) {
        const sm64_saturn_frame_action_t action =
            sm64_saturn_frame_pipeline_step(
                &sourceboot_frame_pipeline, sourceboot_vblank_out_count);
        const uint32_t generation =
            sm64_saturn_frame_pipeline_action_generation(
                &sourceboot_frame_pipeline);
        sourceboot_frame_pipeline_dispatch(action, generation);
    }
}
