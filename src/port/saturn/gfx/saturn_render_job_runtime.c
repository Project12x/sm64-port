#include "saturn_render_job_runtime.h"

#if defined(__sh__)
#include <yaul.h>
#endif

typedef struct sm64_saturn_render_job_runtime {
    sm64_saturn_render_job_queue_t *queue;
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
    if (queue == NULL || callbacks == NULL || s_runtime.active != 0U)
        return false;
    s_runtime.queue = queue;
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
        s_runtime.callbacks == NULL)
        return 0U;
    const uint32_t generation =
        sm64_saturn_render_job_queue_generation(s_runtime.queue);
    if (generation == 0U) return 0U;
    return sm64_saturn_render_job_queue_drain_slave(
        s_runtime.queue, generation, s_runtime.callbacks, s_runtime.context);
}
