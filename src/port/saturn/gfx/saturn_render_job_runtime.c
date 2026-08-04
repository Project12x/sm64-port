#include "saturn_render_job_runtime.h"

#if defined(__sh__)
#include <yaul.h>
#endif

typedef struct sm64_saturn_render_job_runtime {
    sm64_saturn_render_job_queue_t *queue;
    sm64_saturn_render_job_graph_t *graph;
    const sm64_saturn_render_job_callback_table_t *callbacks;
    void *context;
    uint32_t active;
} sm64_saturn_render_job_runtime_t;

#if defined(__sh__)
#define SM64_SATURN_RENDER_JOB_RUNTIME_SHARED __uncached
#else
#define SM64_SATURN_RENDER_JOB_RUNTIME_SHARED
#endif

static sm64_saturn_render_job_runtime_t s_runtime
    SM64_SATURN_RENDER_JOB_RUNTIME_SHARED;

static void runtime_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

#if defined(__sh__)
static void render_job_slave_entry(void)
{
    (void)sm64_saturn_render_job_runtime_poll_slave();
}
#endif

bool sm64_saturn_render_job_runtime_activate(
    sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context)
{
    (void)queue;
    (void)callbacks;
    (void)context;
    /* A raw READY scan would bypass producer dependencies. Do not register a
     * CPU-DUAL callback until the caller supplies the graph. */
    return false;
}

bool sm64_saturn_render_job_runtime_activate_graph(
    sm64_saturn_render_job_graph_t *graph,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context)
{
    if (graph == NULL || graph->queue == NULL || callbacks == NULL ||
        s_runtime.active != 0U)
        return false;
    s_runtime.queue = graph->queue;
    s_runtime.graph = graph;
    s_runtime.callbacks = callbacks;
    s_runtime.context = context;
    runtime_fence();
#if defined(__sh__)
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(render_job_slave_entry);
#endif
    s_runtime.active = 1U;
    runtime_fence();
    return true;
}

void sm64_saturn_render_job_runtime_notify(void)
{
    if (s_runtime.active == 0U || s_runtime.queue == NULL) return;
#if defined(__sh__)
    cpu_dual_slave_notify();
#endif
}

uint16_t sm64_saturn_render_job_runtime_poll_slave(void)
{
    if (s_runtime.active == 0U || s_runtime.queue == NULL ||
        s_runtime.graph == NULL || s_runtime.callbacks == NULL)
        return 0U;
    const uint32_t generation =
        sm64_saturn_render_job_queue_generation(s_runtime.queue);
    if (generation == 0U) return 0U;
    uint16_t completed = 0U;
    uint16_t index = 0U;
    while (sm64_saturn_render_job_graph_claim_slave(
        s_runtime.graph, generation, &index)) {
        const sm64_saturn_render_job_t *const job =
            &s_runtime.queue->jobs[index];
        const uint16_t callback_index = job->callback_id -
            SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT;
        const sm64_saturn_render_job_callback_fn callback =
            callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT
            ? s_runtime.callbacks->entries[callback_index] : NULL;
        if (callback != NULL && callback(job,
            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, s_runtime.context))
            (void)sm64_saturn_render_job_queue_complete(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
        else
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
        (void)sm64_saturn_render_job_graph_propagate_failures(
            s_runtime.graph, generation);
        completed++;
    }
    return completed;
}

uint16_t sm64_saturn_render_job_runtime_drain_master(void)
{
    if (s_runtime.active == 0U || s_runtime.queue == NULL ||
        s_runtime.graph == NULL || s_runtime.callbacks == NULL)
        return 0U;
    const uint32_t generation =
        sm64_saturn_render_job_queue_generation(s_runtime.queue);
    if (generation == 0U) return 0U;
    uint16_t completed = 0U;
    uint16_t index = 0U;
    while (sm64_saturn_render_job_graph_claim_master(
        s_runtime.graph, generation, &index)) {
        const sm64_saturn_render_job_t *const job =
            &s_runtime.queue->jobs[index];
        const uint16_t callback_index = job->callback_id -
            SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT;
        const sm64_saturn_render_job_callback_fn callback =
            callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT
            ? s_runtime.callbacks->entries[callback_index] : NULL;
        if (callback != NULL && callback(job,
            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, s_runtime.context))
            (void)sm64_saturn_render_job_queue_complete(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
        else
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
        (void)sm64_saturn_render_job_graph_propagate_failures(
            s_runtime.graph, generation);
        completed++;
    }
    return completed;
}
