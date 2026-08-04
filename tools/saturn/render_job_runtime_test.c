#include <stdint.h>
#include <stdio.h>

#include "saturn_render_job_runtime.h"
#include "saturn_render_job_graph.h"

static uint32_t s_callbacks;
static bool s_world_admit_done;
static bool s_actor_admit_done;
static bool s_fail_world_admit;

static bool run(const sm64_saturn_render_job_t *job,
                sm64_saturn_render_job_state_t claimed_state, void *context)
{
    (void)context;
    if (job == NULL || job->input_count != 1U ||
        (claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_LOWER &&
        !s_world_admit_done)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_ADMIT &&
        s_fail_world_admit)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_ADMIT)
        s_world_admit_done = true;
    if (job->type == SM64_SATURN_RENDER_JOB_ACTOR_LOWER &&
        !s_actor_admit_done)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_ACTOR_ADMIT)
        s_actor_admit_done = true;
    s_callbacks++;
    return true;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_job_graph_t graph;
    sm64_saturn_render_job_runtime_telemetry_t telemetry;
    /* Model the live four-job graph. Both admits are initially eligible and
     * each lower becomes eligible as soon as its own admit completes. */
    const sm64_saturn_render_job_t jobs[] = {{
        .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_ACTOR_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    }};
    const uint8_t dependencies[] = {0U, 0U, 1U << 0U, 1U << 1U};
    const sm64_saturn_render_job_callback_table_t callbacks = {{
        run, run, run, run
    }};

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_job_graph_init(&graph, &queue);
    if (sm64_saturn_render_job_runtime_activate(&queue, &callbacks, NULL) ||
        !sm64_saturn_render_job_runtime_activate_graph(&graph, &callbacks, NULL) ||
        sm64_saturn_render_job_runtime_activate_graph(&graph, &callbacks, NULL) ||
        !sm64_saturn_render_job_graph_publish(&graph, 7U, jobs, dependencies, 4U) ||
        (sm64_saturn_render_job_runtime_notify(),
         sm64_saturn_render_job_runtime_slave_retired()) ||
        /* A delayed slave models notify -> immediate master drain ordering.
         * The coarse graph permits the master to consume all four jobs. */
        sm64_saturn_render_job_runtime_drain_master() != 4U ||
        sm64_saturn_render_job_runtime_slave_retired() ||
        (sm64_saturn_render_job_runtime_record_master_wait(91U), false) ||
        sm64_saturn_render_job_runtime_poll_slave() != 0U ||
        !sm64_saturn_render_job_runtime_slave_retired() ||
        !sm64_saturn_render_job_runtime_telemetry_snapshot(&telemetry) ||
        telemetry.notified_generation != 7U ||
        telemetry.retired_generation != 7U ||
        telemetry.master_wait_iterations != 91U ||
        telemetry.master_claims[SM64_SATURN_RENDER_JOB_WORLD_ADMIT - 1U] != 1U ||
        telemetry.master_claims[SM64_SATURN_RENDER_JOB_WORLD_LOWER - 1U] != 1U ||
        telemetry.master_claims[SM64_SATURN_RENDER_JOB_ACTOR_ADMIT - 1U] != 1U ||
        telemetry.master_claims[SM64_SATURN_RENDER_JOB_ACTOR_LOWER - 1U] != 1U ||
        telemetry.slave_claims[0] != 0U || telemetry.slave_claims[1] != 0U ||
        telemetry.slave_claims[2] != 0U || telemetry.slave_claims[3] != 0U ||
        telemetry.master_failures != 0U || telemetry.slave_failures != 0U ||
        telemetry.quarantined != 0U || s_callbacks != 4U ||
        !sm64_saturn_render_job_queue_all_terminal(&queue, 7U)) {
        fputs("render job runtime lifecycle failed\n", stderr);
        return 1;
    }
    if (!sm64_saturn_render_job_queue_reset_retired(&queue, 7U)) {
        fputs("render job runtime retirement failed\n", stderr);
        return 1;
    }

    sm64_saturn_render_job_t failed_jobs[4];
    for (uint16_t index = 0U; index < 4U; index++) {
        failed_jobs[index] = jobs[index];
        failed_jobs[index].snapshot_generation = 8U;
    }
    s_world_admit_done = false;
    s_actor_admit_done = false;
    s_fail_world_admit = true;
    if (!sm64_saturn_render_job_graph_publish(&graph, 8U, failed_jobs,
                                               dependencies, 4U) ||
        (sm64_saturn_render_job_runtime_notify(), false) ||
        sm64_saturn_render_job_runtime_poll_slave() != 3U ||
        (sm64_saturn_render_job_runtime_record_master_wait(0U), false) ||
        !sm64_saturn_render_job_runtime_telemetry_snapshot(&telemetry) ||
        telemetry.notified_generation != 8U ||
        telemetry.retired_generation != 8U ||
        telemetry.slave_claims[0] != 1U ||
        telemetry.slave_claims[1] != 0U ||
        telemetry.slave_claims[2] != 1U ||
        telemetry.slave_claims[3] != 1U ||
        telemetry.slave_failures != 1U || telemetry.quarantined != 1U ||
        !sm64_saturn_render_job_queue_all_terminal(&queue, 8U)) {
        fputs("render job runtime failure telemetry failed\n", stderr);
        return 1;
    }

    puts("render job runtime fixture: PASS");
    return 0;
}
