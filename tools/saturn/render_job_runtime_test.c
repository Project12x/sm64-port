#include <stdint.h>
#include <stdio.h>

#include "saturn_render_job_runtime.h"

static uint32_t s_callbacks;

static bool run(const sm64_saturn_render_job_t *job,
                sm64_saturn_render_job_state_t claimed_state, void *context)
{
    (void)context;
    return job->input_count == 1U &&
        claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE &&
        ++s_callbacks == 1U;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    const sm64_saturn_render_job_t job = {
        .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
        .snapshot_generation = 7U,
        .input_count = 1U,
        .output_capacity = 1U,
    };
    const sm64_saturn_render_job_callback_table_t callbacks = {{
        run, NULL, NULL, NULL
    }};

    sm64_saturn_render_job_queue_init(&queue);
    if (!sm64_saturn_render_job_runtime_activate(&queue, &callbacks, NULL) ||
        sm64_saturn_render_job_runtime_activate(&queue, &callbacks, NULL) ||
        !sm64_saturn_render_job_queue_publish(&queue, 7U, &job, 1U) ||
        sm64_saturn_render_job_runtime_poll_slave() != 1U ||
        s_callbacks != 1U ||
        !sm64_saturn_render_job_queue_all_terminal(&queue, 7U)) {
        fputs("render job runtime lifecycle failed\n", stderr);
        return 1;
    }
    puts("render job runtime fixture: PASS");
    return 0;
}
