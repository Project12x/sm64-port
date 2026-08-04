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
    volatile uint32_t notify_sequence;
    volatile uint32_t retired_sequence;
    sm64_saturn_render_job_runtime_telemetry_t telemetry;
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

static void telemetry_reset(uint32_t generation, uint32_t sequence)
{
    s_runtime.telemetry.notified_generation = generation;
    s_runtime.telemetry.retired_generation = 0U;
    s_runtime.telemetry.notify_sequence = sequence;
    s_runtime.telemetry.retired_sequence = 0U;
    for (uint16_t index = 0U;
         index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT; index++) {
        s_runtime.telemetry.master_claims[index] = 0U;
        s_runtime.telemetry.slave_claims[index] = 0U;
    }
    s_runtime.telemetry.master_wait_iterations = 0U;
    s_runtime.telemetry.master_failures = 0U;
    s_runtime.telemetry.slave_failures = 0U;
    s_runtime.telemetry.quarantined = 0U;
}

static void telemetry_retire(uint32_t generation, uint32_t sequence)
{
    s_runtime.telemetry.retired_generation = generation;
    s_runtime.telemetry.retired_sequence = sequence;
}

#if defined(__sh__)
static void render_job_slave_entry(void)
{
    const uint32_t notified = s_runtime.notify_sequence;
    const uint32_t generation = s_runtime.telemetry.notified_generation;
    (void)sm64_saturn_render_job_runtime_poll_slave();
    runtime_fence();
    s_runtime.retired_sequence = notified;
    telemetry_retire(generation, notified);
    runtime_fence();
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
    uint32_t sequence = s_runtime.notify_sequence + 1U;
    if (sequence == 0U) sequence = 1U;
    s_runtime.notify_sequence = sequence;
    telemetry_reset(sm64_saturn_render_job_queue_generation(s_runtime.queue),
                    sequence);
    runtime_fence();
#if defined(__sh__)
    cpu_dual_slave_notify();
#endif
}

bool sm64_saturn_render_job_runtime_slave_retired(void)
{
    runtime_fence();
    return s_runtime.active != 0U && s_runtime.notify_sequence != 0U &&
        s_runtime.retired_sequence == s_runtime.notify_sequence;
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
            sm64_saturn_render_job_queue_claimed_job(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
        if (job == NULL) {
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
            (void)sm64_saturn_render_job_graph_propagate_failures(
                s_runtime.graph, generation);
            completed++;
            continue;
        }
        const uint16_t callback_index = job->callback_id -
            SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT;
        if (callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT)
            s_runtime.telemetry.slave_claims[callback_index]++;
        const sm64_saturn_render_job_callback_fn callback =
            callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT
            ? s_runtime.callbacks->entries[callback_index] : NULL;
        if (callback != NULL && callback(job,
            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, s_runtime.context))
            (void)sm64_saturn_render_job_queue_complete(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
        else {
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE);
            s_runtime.telemetry.slave_failures++;
        }
        (void)sm64_saturn_render_job_graph_propagate_failures(
            s_runtime.graph, generation);
        completed++;
    }
    runtime_fence();
#if !defined(__sh__)
    s_runtime.retired_sequence = s_runtime.notify_sequence;
    telemetry_retire(generation, s_runtime.notify_sequence);
    runtime_fence();
#endif
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
            sm64_saturn_render_job_queue_claimed_job(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
        if (job == NULL) {
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
            (void)sm64_saturn_render_job_graph_propagate_failures(
                s_runtime.graph, generation);
            completed++;
            continue;
        }
        const uint16_t callback_index = job->callback_id -
            SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT;
        if (callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT)
            s_runtime.telemetry.master_claims[callback_index]++;
        const sm64_saturn_render_job_callback_fn callback =
            callback_index < SM64_SATURN_RENDER_JOB_CALLBACK_COUNT
            ? s_runtime.callbacks->entries[callback_index] : NULL;
        if (callback != NULL && callback(job,
            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, s_runtime.context))
            (void)sm64_saturn_render_job_queue_complete(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
        else {
            (void)sm64_saturn_render_job_queue_fail(
                s_runtime.queue, generation, index,
                SM64_SATURN_RENDER_JOB_CLAIMED_MASTER);
            s_runtime.telemetry.master_failures++;
        }
        (void)sm64_saturn_render_job_graph_propagate_failures(
            s_runtime.graph, generation);
        completed++;
    }
    return completed;
}

void sm64_saturn_render_job_runtime_record_master_wait(uint32_t iterations)
{
    s_runtime.telemetry.master_wait_iterations = iterations;
    s_runtime.telemetry.quarantined = 0U;
    if (s_runtime.queue != NULL) {
        const uint32_t generation =
            sm64_saturn_render_job_queue_generation(s_runtime.queue);
        /* Do not read queue->count through the cached alias here. The queue
         * accessor selects P2 and rejects indices beyond the live count. */
        for (uint16_t index = 0U;
             index < SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY; index++)
            if (sm64_saturn_render_job_queue_state(
                    s_runtime.queue, generation, index) ==
                SM64_SATURN_RENDER_JOB_QUARANTINED)
                s_runtime.telemetry.quarantined++;
    }
    runtime_fence();
}

bool sm64_saturn_render_job_runtime_telemetry_snapshot(
    sm64_saturn_render_job_runtime_telemetry_t *telemetry)
{
    if (telemetry == NULL || s_runtime.active == 0U) return false;
    runtime_fence();
    *telemetry = s_runtime.telemetry;
    runtime_fence();
    return true;
}
