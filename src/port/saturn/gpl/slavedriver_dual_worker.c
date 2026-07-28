/* See slavedriver_dual_worker.h for provenance and reuse mode. */
#include "slavedriver_dual_worker.h"

#if defined(__sh__)
#include <yaul.h>
#endif

#if defined(__sh__)
#define SATURN_DUAL_UNCACHED __uncached
#else
#define SATURN_DUAL_UNCACHED
#endif

typedef struct sm64_saturn_dual_worker_control {
    volatile uint32_t active;
    volatile uint32_t done;
    volatile uint32_t cancel;
    volatile uint32_t slave_busy_ticks;
    volatile uint16_t begin;
    volatile uint16_t end;
    volatile sm64_saturn_dual_worker_fn fn;
    volatile void *context;
} sm64_saturn_dual_worker_control_t;

#if defined(__sh__)
static sm64_saturn_dual_worker_control_t s_control SATURN_DUAL_UNCACHED;
static bool s_initialized;

static void dual_slave_entry(void)
{
    if (s_control.active == 0U || s_control.fn == NULL) return;
    const uint16_t begin = s_control.begin;
    const uint16_t end = s_control.end;
    const uint16_t start_ticks = cpu_frt_count_get();
    s_control.fn((void *)s_control.context, begin, end);
    s_control.slave_busy_ticks =
        (uint16_t)(cpu_frt_count_get() - start_ticks);
    s_control.done = 1U;
}

void sm64_saturn_dual_worker_init(void)
{
    if (s_initialized) return;
    s_control = (sm64_saturn_dual_worker_control_t){0};
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(dual_slave_entry);
    s_initialized = true;
}

bool sm64_saturn_dual_worker_cancelled(void)
{
    return s_control.cancel != 0U;
}

bool sm64_saturn_dual_worker_run(sm64_saturn_dual_worker_fn fn,
                                 void *context, uint16_t count,
                                 uint16_t slave_begin,
                                 sm64_saturn_dual_worker_stats_t *stats)
{
    if (stats != NULL) *stats = (sm64_saturn_dual_worker_stats_t){0};
    if (fn == NULL || slave_begin >= count) {
        if (fn != NULL) fn(context, 0U, count);
        return true;
    }
    sm64_saturn_dual_worker_init();
    s_control.fn = fn;
    s_control.context = context;
    s_control.begin = slave_begin;
    s_control.end = count;
    s_control.cancel = 0U;
    s_control.done = 0U;
    s_control.slave_busy_ticks = 0U;
    s_control.active = 1U;

    cpu_dual_slave_notify();
    fn(context, 0U, slave_begin);

    /* A bounded poll is the normal path. The callback is required to check
     * cancellation, so a missed notification cannot leave an unbounded
     * foreground spin or overlap a serial fallback. */
    const uint16_t wait_start = cpu_frt_count_get();
    uint32_t spins = 0U;
    while (s_control.done == 0U && spins++ < 10000000U) {
        /* polling-mode slave runs independently; this loop only observes the
         * uncached completion word. */
    }
    const bool completed = s_control.done != 0U;
    const uint16_t master_wait_ticks =
        (uint16_t)(cpu_frt_count_get() - wait_start);
    if (!completed) {
        s_control.cancel = 1U;
        uint32_t cancel_spins = 0U;
        while (s_control.done == 0U && cancel_spins++ < 1000000U) {
            /* bounded transform callbacks observe cancel promptly */
        }
    }
    s_control.active = 0U;
    if (stats != NULL) {
        stats->slave_jobs_completed = completed ? 1U : 0U;
        stats->slave_busy_ticks = s_control.slave_busy_ticks;
        stats->master_wait_ticks = master_wait_ticks;
        stats->slave_timeouts = completed ? 0U : 1U;
    }
    /* A serial fallback must not inherit the cancellation latch from the
     * failed dispatch. Delayed polling notifications observe active==0 and
     * return without touching the caller's next frame. */
    s_control.cancel = 0U;
    return completed;
}
#else
bool sm64_saturn_dual_worker_cancelled(void) { return false; }
void sm64_saturn_dual_worker_init(void) {}
bool sm64_saturn_dual_worker_run(sm64_saturn_dual_worker_fn fn,
                                 void *context, uint16_t count,
                                 uint16_t slave_begin,
                                 sm64_saturn_dual_worker_stats_t *stats)
{
    if (stats != NULL) *stats = (sm64_saturn_dual_worker_stats_t){0};
    if (fn != NULL) fn(context, 0U, count);
    (void)slave_begin;
    return true;
}
#endif
