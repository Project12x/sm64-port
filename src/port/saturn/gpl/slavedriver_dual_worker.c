/* See slavedriver_dual_worker.h for provenance and reuse mode. */
#include "slavedriver_dual_worker.h"

#if defined(__sh__)
#include <yaul.h>
#elif defined(_WIN32)
#include <windows.h>
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

static inline uint32_t dual_control_read(const volatile uint32_t *value)
{
#if defined(_WIN32) && !defined(__sh__)
    return (uint32_t)InterlockedCompareExchange((volatile LONG *)value, 0L,
                                                 0L);
#else
    return *value;
#endif
}

static inline void dual_control_write(volatile uint32_t *value, uint32_t next)
{
#if defined(_WIN32) && !defined(__sh__)
    InterlockedExchange((volatile LONG *)value, (LONG)next);
#else
    *value = next;
#endif
}

#if defined(__sh__) || defined(_WIN32)
static sm64_saturn_dual_worker_control_t s_control SATURN_DUAL_UNCACHED;
static bool s_initialized;
#if defined(SM64_SATURN_DUAL_WORKER_TEST_HOOK) && !defined(__sh__)
static volatile uint32_t s_test_force_timeout;

void sm64_saturn_dual_worker_test_force_timeout(bool enabled)
{
    dual_control_write(&s_test_force_timeout, enabled ? 1U : 0U);
}
#endif

#if defined(__sh__)
static void dual_slave_entry(void)
{
    if (dual_control_read(&s_control.active) == 0U || s_control.fn == NULL) return;
    const uint16_t begin = s_control.begin;
    const uint16_t end = s_control.end;
    const uint16_t start_ticks = cpu_frt_count_get();
    s_control.fn((void *)s_control.context, begin, end);
    s_control.slave_busy_ticks =
        (uint16_t)(cpu_frt_count_get() - start_ticks);
    dual_control_write(&s_control.done, 1U);
}
#else
static DWORD WINAPI dual_slave_entry(void *opaque)
{
    (void)opaque;
    if (dual_control_read(&s_control.active) == 0U || s_control.fn == NULL)
        return 0UL;
    const uint16_t begin = s_control.begin;
    const uint16_t end = s_control.end;
    s_control.fn((void *)s_control.context, begin, end);
    dual_control_write(&s_control.done, 1U);
    return 0UL;
}
#endif

void sm64_saturn_dual_worker_init(void)
{
    if (s_initialized) return;
    s_control = (sm64_saturn_dual_worker_control_t){0};
#if defined(__sh__)
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(dual_slave_entry);
#endif
    s_initialized = true;
}

bool sm64_saturn_dual_worker_is_idle(void)
{
    return dual_control_read(&s_control.active) == 0U;
}

bool sm64_saturn_dual_worker_cancelled(void)
{
    return dual_control_read(&s_control.cancel) != 0U;
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
    dual_control_write(&s_control.cancel, 0U);
    dual_control_write(&s_control.done, 0U);
    s_control.slave_busy_ticks = 0U;
    dual_control_write(&s_control.active, 1U);

#if defined(__sh__)
    cpu_dual_slave_notify();
#else
    HANDLE slave = CreateThread(NULL, 0U, dual_slave_entry, NULL, 0U, NULL);
    if (slave == NULL) {
        dual_control_write(&s_control.active, 0U);
        dual_control_write(&s_control.cancel, 0U);
        if (stats != NULL) stats->slave_timeouts = 1U;
        return false;
    }
#endif
    fn(context, 0U, slave_begin);

#if defined(__sh__)
    const uint16_t wait_start = cpu_frt_count_get();
#endif
    uint32_t spins = 0U;
    while (dual_control_read(&s_control.done) == 0U && spins++ < 10000000U
#if defined(SM64_SATURN_DUAL_WORKER_TEST_HOOK) && !defined(__sh__)
           && dual_control_read(&s_test_force_timeout) == 0U
#endif
    ) {
        /* polling-mode slave runs independently; this loop only observes the
         * uncached completion word. */
    }
#if defined(__sh__)
    const uint16_t master_wait_ticks =
        (uint16_t)(cpu_frt_count_get() - wait_start);
#else
    const uint16_t master_wait_ticks = 0U;
#endif
    const bool timed_out = dual_control_read(&s_control.done) == 0U;
    if (timed_out) {
        dual_control_write(&s_control.cancel, 1U);
        /* Do not clear active/cancel or return to a fallback until the slave
         * has positively retired.  Bounded callbacks observe cancel between
         * items; a broken callback may stall here, but it may never race a
         * caller's result-span reset. */
        while (dual_control_read(&s_control.done) == 0U) {
#if defined(_WIN32)
            SwitchToThread();
#endif
        }
    }
#if defined(_WIN32)
    WaitForSingleObject(slave, INFINITE);
    CloseHandle(slave);
#endif
    dual_control_write(&s_control.active, 0U);
    if (stats != NULL) {
        stats->slave_jobs_completed = timed_out ? 0U : 1U;
        stats->slave_busy_ticks = s_control.slave_busy_ticks;
        stats->master_wait_ticks = master_wait_ticks;
        stats->slave_timeouts = timed_out ? 1U : 0U;
    }
    /* A serial fallback must not inherit the cancellation latch from the
     * failed dispatch. Delayed polling notifications observe active==0 and
     * return without touching the caller's next frame. */
    dual_control_write(&s_control.cancel, 0U);
#if defined(SM64_SATURN_DUAL_WORKER_TEST_HOOK) && !defined(__sh__)
    dual_control_write(&s_test_force_timeout, 0U);
#endif
    return !timed_out;
}
#else
bool sm64_saturn_dual_worker_cancelled(void) { return false; }
void sm64_saturn_dual_worker_init(void) {}
bool sm64_saturn_dual_worker_is_idle(void) { return true; }
bool sm64_saturn_dual_worker_run(sm64_saturn_dual_worker_fn fn,
                                 void *context, uint16_t count,
                                 uint16_t slave_begin,
                                 sm64_saturn_dual_worker_stats_t *stats)
{
    if (stats != NULL) *stats = (sm64_saturn_dual_worker_stats_t){0};
#if defined(SM64_SATURN_DUAL_WORKER_SIMULATE_TIMEOUT)
    (void)fn;
    (void)context;
    (void)count;
    (void)slave_begin;
    if (stats != NULL) stats->slave_timeouts = 1U;
    return false;
#else
    if (fn != NULL) fn(context, 0U, count);
    (void)slave_begin;
    return true;
#endif
}
#endif
