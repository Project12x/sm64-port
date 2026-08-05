#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "saturn_dual_frame_bank.h"
#include "saturn_frame_pipeline.h"
#include "saturn_lod_lifetime.h"
#include "saturn_render_job_graph.h"
#include "saturn_render_job_runtime.h"
#include "saturn_render_lifecycle.h"
#include "saturn_render_overlap_phase.h"

typedef struct integration_lod_storage {
    uint8_t tiers[1];
    uint8_t cluster_lod[4];
} integration_lod_storage_t;

typedef struct integration_context {
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_job_graph_t graph;
    sm64_saturn_render_lifecycle_t lifecycle;
    sm64_saturn_render_overlap_phase_t phase;
    sm64_saturn_lod_lifetime_t lod;
    sm64_saturn_render_job_runtime_telemetry_t telemetry;
    integration_lod_storage_t lod_storage;
    integration_lod_storage_t *master_lod_storage;
    integration_lod_storage_t *worker_lod_storage;
    uint32_t clock;
    uint32_t generation;
    uint32_t notify_boundary;
    uint32_t finalize_end;
    uint32_t marker_clock_reads;
    uint8_t worker_lod;
    bool fail_admit;
    bool phase_ok;
} integration_context_t;

static integration_lod_storage_t *integration_lod_storage_cache_through(
    integration_context_t *context)
{
    return context == NULL ? NULL : (integration_lod_storage_t *)
        sm64_saturn_dual_frame_cache_through(&context->lod_storage);
}

static const int s_snapshot_identity;
static const int s_bank_identity;

static bool integration_job(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *opaque)
{
    integration_context_t *const context = opaque;
    if (context == NULL || job == NULL ||
        claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_ADMIT)
        return !context->fail_admit;
    if (job->type != SM64_SATURN_RENDER_JOB_WORLD_LOWER)
        return false;
    context->worker_lod_storage =
        integration_lod_storage_cache_through(context);
    if (context->worker_lod_storage == NULL ||
        context->worker_lod_storage != context->master_lod_storage ||
        context->lod.tiers != context->worker_lod_storage->tiers ||
        context->lod.cluster_lod != context->worker_lod_storage->cluster_lod)
        return false;
    uint8_t transition = 0U;
    const saturn_lod_thresholds_t thresholds =
        saturn_lod_default_thresholds();
    return sm64_saturn_lod_lifetime_select(
        &context->lod, job->snapshot_generation, 0U,
        6500, 45U, &thresholds, &context->worker_lod, &transition);
}

static bool integration_prepare(void *opaque, uint32_t generation)
{
    integration_context_t *const context = opaque;
    const sm64_saturn_render_job_t jobs[] = {{
        .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
        .snapshot_generation = generation,
        .input_count = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
        .snapshot_generation = generation,
        .input_count = 1U,
        .output_capacity = 1U,
    }};
    const uint8_t dependencies[] = {0U, 1U << 0U};
    return context != NULL &&
        sm64_saturn_render_job_graph_publish(
            &context->graph, generation, jobs, dependencies, 2U);
}

static void integration_notify(void *opaque)
{
    integration_context_t *const context = opaque;
    context->clock = context->notify_boundary;
    sm64_saturn_render_job_runtime_notify();
    context->clock++;
}

static bool integration_slave_retired(void *opaque)
{
    (void)opaque;
    return sm64_saturn_render_job_runtime_slave_retired();
}

static uint16_t integration_drain_master(void *opaque)
{
    (void)opaque;
    return sm64_saturn_render_job_runtime_drain_master();
}

static bool integration_finalize(void *opaque, uint32_t generation,
                                 uint16_t master_jobs)
{
    integration_context_t *const context = opaque;
    (void)master_jobs;
    context->clock = context->finalize_end;
    sm64_saturn_render_job_runtime_refresh_terminal_telemetry();
    if (!sm64_saturn_render_job_runtime_telemetry_snapshot(
            &context->telemetry) ||
        !sm64_saturn_render_job_queue_all_terminal(
            &context->queue, generation))
        return false;
    const bool reset = sm64_saturn_render_job_queue_reset_retired(
        &context->queue, generation);
    return reset && context->telemetry.master_failures == 0U &&
        context->telemetry.slave_failures == 0U &&
        context->telemetry.quarantined == 0U;
}

static void integration_quarantine(void *opaque, uint32_t generation)
{
    integration_context_t *const context = opaque;
    for (uint16_t index = 0U;
         index < SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY; index++)
        (void)sm64_saturn_render_job_queue_quarantine_ready(
            &context->queue, generation, index);
    if (sm64_saturn_render_job_queue_all_terminal(
            &context->queue, generation))
        (void)sm64_saturn_render_job_queue_reset_retired(
            &context->queue, generation);
}

static uint32_t integration_marker_clock(void *opaque)
{
    integration_context_t *const context = opaque;
    context->marker_clock_reads++;
    const uint32_t marker_vblank = context->clock;
#if defined(SM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_NOTIFY_MARKER) || \
    defined(SM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_RETIRE_MARKER)
    context->clock++;
#endif
    return marker_vblank;
}

static void integration_runtime_marker(
    void *opaque, sm64_saturn_render_job_runtime_marker_t marker,
    uint32_t generation, uint32_t sequence, uint32_t marker_vblank)
{
    integration_context_t *const context = opaque;
    (void)sequence;
    bool accepted = false;
    if (marker == SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_NOTIFIED)
        accepted = sm64_saturn_render_overlap_phase_notification_published(
            &context->phase, generation, marker_vblank);
    else if (marker == SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_RETIRED)
        accepted = sm64_saturn_render_overlap_phase_retirement_published(
            &context->phase, generation, marker_vblank);
    context->phase_ok = context->phase_ok && accepted;
}

static const sm64_saturn_render_lifecycle_ops_t s_lifecycle_ops = {
    .prepare_publish = integration_prepare,
    .notify = integration_notify,
    .slave_retired = integration_slave_retired,
    .drain_master = integration_drain_master,
    .finalize = integration_finalize,
    .quarantine = integration_quarantine,
};

static const sm64_saturn_render_job_callback_table_t s_callbacks = {{
    integration_job, integration_job, NULL, NULL,
}};

static bool begin_generation(integration_context_t *context,
                             uint32_t generation,
                             uint32_t construction_begin,
                             uint32_t notify_boundary)
{
    context->generation = generation;
    context->clock = construction_begin;
    context->notify_boundary = notify_boundary;
    context->phase_ok = true;
    context->marker_clock_reads = 0U;
    context->master_lod_storage =
        integration_lod_storage_cache_through(context);
    if (context->master_lod_storage == NULL) return false;
    return sm64_saturn_render_overlap_phase_begin(
               &context->phase, generation, construction_begin) &&
        sm64_saturn_render_overlap_phase_bind(
               &context->phase, generation,
               &s_snapshot_identity, &s_bank_identity) &&
        sm64_saturn_lod_lifetime_begin(&context->lod, generation) &&
        sm64_saturn_render_lifecycle_start(
               &context->lifecycle, &s_lifecycle_ops, context, generation);
}

static int test_pending_generation_retains_lod_and_complete_phase_accounting(
    integration_context_t *context)
{
    sm64_saturn_frame_pipeline_t pipeline;
    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 6U);
    if (sm64_saturn_frame_pipeline_step(&pipeline, 104U) !=
            SM64_SATURN_FRAME_RUN_SIM_TICK ||
        sm64_saturn_frame_pipeline_action_generation(&pipeline) != 7U)
        return 10;
    if (!begin_generation(context, 7U, 10U, 12U)) return 11;
    if (sm64_saturn_frame_pipeline_step(&pipeline, 104U) !=
            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS)
        return 12;
    if (!sm64_saturn_render_overlap_phase_retains(
            &context->phase, 7U,
            &s_snapshot_identity, &s_bank_identity))
        return 13;
    if (sm64_saturn_frame_pipeline_step(&pipeline, 104U) !=
            SM64_SATURN_FRAME_RUN_SIM_TICK ||
        sm64_saturn_frame_pipeline_action_generation(&pipeline) != 8U)
        return 14;

    context->master_lod_storage->tiers[0] = SATURN_LOD_FAR;
    context->master_lod_storage->cluster_lod[0] = 0x7FU;
    if (sm64_saturn_lod_lifetime_observe_scene(
            &context->lod, true, 2, 1))
        return 15;
    if (context->master_lod_storage->tiers[0] != SATURN_LOD_FAR ||
        context->master_lod_storage->cluster_lod[0] != 0x7FU)
        return 16;
    uint8_t wrong_generation_tier = 0xA5U;
    uint8_t wrong_generation_transition = 0x5AU;
    const saturn_lod_thresholds_t thresholds =
        saturn_lod_default_thresholds();
    if (sm64_saturn_lod_lifetime_select(
            &context->lod, 8U, 0U, 6500, 45U, &thresholds,
            &wrong_generation_tier, &wrong_generation_transition) ||
        context->master_lod_storage->tiers[0] != SATURN_LOD_FAR ||
        wrong_generation_tier != 0xA5U ||
        wrong_generation_transition != 0x5AU)
        return 26;
    if (sm64_saturn_render_lifecycle_poll(
            &context->lifecycle, &s_lifecycle_ops, context, 7U) !=
            SM64_SATURN_RENDER_LIFECYCLE_PENDING)
        return 17;

    context->clock = 16U;
    if (sm64_saturn_render_job_runtime_poll_slave() != 2U)
        return 18;
    context->clock = 17U;
    context->finalize_end = 18U;
    if (sm64_saturn_render_lifecycle_poll(
            &context->lifecycle, &s_lifecycle_ops, context, 7U) !=
            SM64_SATURN_RENDER_LIFECYCLE_COMPLETE)
        return 19;
    if (!context->phase_ok ||
        !sm64_saturn_render_overlap_phase_terminal(
            &context->phase, 7U,
            &s_snapshot_identity, &s_bank_identity, context->clock))
        return 20;
    if (!sm64_saturn_lod_lifetime_finish(&context->lod, 7U)) return 21;
    if (context->worker_lod != SATURN_LOD_FAR) return 22;
    if (context->master_lod_storage->tiers[0] != SATURN_LOD_NEAR ||
        context->master_lod_storage->cluster_lod[0] != 0U ||
        context->worker_lod_storage != context->master_lod_storage)
        return 23;
    if (context->phase.construction_vblank_crossings != 4U ||
        context->phase.construction_count != 1U ||
        context->phase.slave_work_vblank_crossings != 4U ||
        context->phase.slave_work_count != 1U ||
        context->phase.master_finalize_vblank_crossings != 2U ||
        context->phase.master_finalize_count != 1U)
        return 24;
    if (context->phase.notification_vblank != 12U ||
        context->phase.retirement_vblank != 16U ||
        context->marker_clock_reads != 2U)
        return 27;

    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 7U) ||
        sm64_saturn_frame_pipeline_step(&pipeline, 104U) !=
            SM64_SATURN_FRAME_POLL_TRANSFERS ||
        !sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 7U) ||
        sm64_saturn_frame_pipeline_step(&pipeline, 104U) !=
            SM64_SATURN_FRAME_PUBLISH_FRAME ||
        !sm64_saturn_frame_pipeline_publish_complete(&pipeline, 7U, true) ||
        pipeline.render_generation != 8U)
        return 25;
    return 0;
}

static int test_failed_generation_publishes_nonzero_quarantine(
    integration_context_t *context)
{
    context->fail_admit = true;
    if (!begin_generation(context, 8U, 20U, 21U)) return 30;
    context->master_lod_storage->tiers[0] = SATURN_LOD_FAR;
    context->master_lod_storage->cluster_lod[0] = 0x55U;
    if (sm64_saturn_lod_lifetime_observe_scene(
            &context->lod, true, 3, 1) ||
        context->master_lod_storage->tiers[0] != SATURN_LOD_FAR ||
        context->master_lod_storage->cluster_lod[0] != 0x55U)
        return 35;
    context->clock = 22U;
    if (sm64_saturn_render_job_runtime_poll_slave() != 1U) return 31;
    context->clock = 23U;
    context->finalize_end = 23U;
    if (sm64_saturn_render_lifecycle_poll(
            &context->lifecycle, &s_lifecycle_ops, context, 8U) !=
            SM64_SATURN_RENDER_LIFECYCLE_FAILED)
        return 32;
    if (context->master_lod_storage->tiers[0] != SATURN_LOD_FAR ||
        context->master_lod_storage->cluster_lod[0] != 0x55U)
        return 36;
    if (!context->phase_ok ||
        !sm64_saturn_render_overlap_phase_terminal(
            &context->phase, 8U,
            &s_snapshot_identity, &s_bank_identity, context->clock) ||
        !sm64_saturn_lod_lifetime_finish(&context->lod, 8U))
        return 33;
    if (context->telemetry.slave_failures != 1U ||
        context->telemetry.quarantined != 1U ||
        context->master_lod_storage->tiers[0] != SATURN_LOD_NEAR ||
        context->master_lod_storage->cluster_lod[0] != 0U)
        return 34;
    return 0;
}

int main(void)
{
    integration_context_t context = {0};
    sm64_saturn_render_job_queue_init(&context.queue);
    sm64_saturn_render_job_graph_init(&context.graph, &context.queue);
    sm64_saturn_render_overlap_phase_init(&context.phase);
    integration_lod_storage_t *const lod_storage =
        integration_lod_storage_cache_through(&context);
    sm64_saturn_lod_lifetime_init(
        &context.lod, lod_storage->tiers, sizeof(lod_storage->tiers),
        lod_storage->cluster_lod, sizeof(lod_storage->cluster_lod));
    if (!sm64_saturn_render_job_runtime_activate_graph(
            &context.graph, &s_callbacks, &context))
        return 2;
    if (!sm64_saturn_render_job_runtime_observe_markers(
            integration_runtime_marker, integration_marker_clock, &context))
        return 4;
    if (!sm64_saturn_lod_lifetime_observe_scene(
            &context.lod, true, 1, 1))
        return 3;
    int failure =
        test_pending_generation_retains_lod_and_complete_phase_accounting(
            &context);
    if (failure != 0) return failure;
    failure = test_failed_generation_publishes_nonzero_quarantine(&context);
    if (failure != 0) return failure;
    puts("render overlap production integration: PASS");
    return 0;
}
