#include <stdint.h>
#include <stdio.h>

#include "saturn_render_job_runtime.h"
#include "saturn_render_job_graph.h"

static uint32_t s_callbacks;
static bool s_world_admit_done;

static bool run(const sm64_saturn_render_job_t *job,
                sm64_saturn_render_job_state_t claimed_state, void *context)
{
    (void)context;
    if (job == NULL || job->input_count != 1U ||
        claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_LOWER &&
        !s_world_admit_done)
        return false;
    if (job->type == SM64_SATURN_RENDER_JOB_WORLD_ADMIT)
        s_world_admit_done = true;
    s_callbacks++;
    return true;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_job_graph_t graph;
    /* Publish the consumer first: a queue-only drain would claim it before
     * the producer. The graph-aware runtime must still execute ADMIT first. */
    const sm64_saturn_render_job_t jobs[] = {{
        .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
        .snapshot_generation = 7U,
        .input_offset = 1U,
        .input_count = 1U,
        .output_offset = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    }};
    const uint8_t dependencies[] = {1U << 1U, 0U};
    const sm64_saturn_render_job_callback_table_t callbacks = {{
        run, run, NULL, NULL
    }};

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_job_graph_init(&graph, &queue);
    if (sm64_saturn_render_job_runtime_activate(&queue, &callbacks, NULL) ||
        !sm64_saturn_render_job_runtime_activate_graph(&graph, &callbacks, NULL) ||
        sm64_saturn_render_job_runtime_activate_graph(&graph, &callbacks, NULL) ||
        !sm64_saturn_render_job_graph_publish(&graph, 7U, jobs, dependencies, 2U) ||
        sm64_saturn_render_job_runtime_poll_slave() != 2U ||
        s_callbacks != 2U ||
        !sm64_saturn_render_job_queue_all_terminal(&queue, 7U)) {
        fputs("render job runtime lifecycle failed\n", stderr);
        return 1;
    }
    puts("render job runtime fixture: PASS");
    return 0;
}
